#!/usr/bin/env python3
"""Verify a DIMACS 9th Implementation Challenge (Shortest Paths) graph file.

Supports the USA road network "*_d" distance graphs (``.gr``) and their
coordinate side-car files (``.co``).  Input may be plain text or gzip-compressed.
Streaming implementation: memory use is O(n) bytes (one presence byte per node)
plus a small amount of state, so it works for the multi-hundred-MB ``E`` graph.

Checks performed
----------------
``.gr`` (DIMACS shortest-path graph):
  * the ``p sp <n> <m>`` problem line exists (and comes before data lines);
  * every ``a u v w`` line has 3 integer fields, ``w`` is an integer weight;
  * the number of ``a`` lines equals ``m``;
  * every node id (arc tail and head) is in ``[1, n]``;
  * the maximum node id equals ``n``;
  * records actual ``n``/``m``, min/max arc weight and how many of ``1..n``
    appear as arc endpoints (``missing_nodes`` is a warning, not a failure --
    but for the official USA road graphs it is expected to be 0).

``.co`` (DIMACS aux coordinate file):
  * the ``p aux sp co <n>`` problem line exists;
  * every ``v <id> <lon> <lat>`` line has a valid node id and numeric coords;
  * the number of ``v`` lines equals ``n``;
  * every node id is in ``[1, n]`` and the maximum id equals ``n``.

Optionally extracts the gzip input to a plain-text file in the same streaming
pass (``--extract-to``), so the decompressed payload is byte-identical to what
was validated.  gzip CRC is verified by Python's gzip module at EOF.

Exit status: 0 = all hard checks passed, 1 = validation failed,
2 = could not read/decompress the file.

Usage
-----
    python verify-dimacs.py data/dimacs/raw/USA-road-d.NY.gr.gz \
        --extract-to data/dimacs/USA-road-d.NY.gr --json report.json
"""

from __future__ import annotations

import argparse
import gzip
import io
import json
import os
import sys


class NullWriter(io.RawIOBase):
    def writable(self) -> bool:  # noqa: D102
        return True

    def write(self, b) -> int:  # noqa: D102
        return len(b)


def _fail(errs: list[str], msg: str, limit: int = 20) -> None:
    """Record a failure, keeping the error list bounded for huge files."""
    if len(errs) < limit:
        errs.append(msg)


def verify(path: str, extract_to: str | None) -> dict:
    gz = path.lower().endswith(".gz")
    fh = gzip.open(path, "rb") if gz else open(path, "rb")

    out = None
    if extract_to:
        os.makedirs(os.path.dirname(os.path.abspath(extract_to)) or ".", exist_ok=True)
        out = open(extract_to, "wb")

    kind = "co" if path.lower().endswith(".co") or path.lower().endswith(".co.gz") else "gr"
    kind = "co" if ".co" in os.path.basename(path).lower() else "gr"

    header: str | None = None
    n: int | None = None
    m: int | None = None
    a_cnt = v_cnt = 0
    malformed = 0
    out_of_range = 0
    dup_nodes = 0
    min_id: int | None = None
    max_id: int | None = None
    w_min: int | None = None
    w_max: int | None = None
    coords = 0
    present: bytearray | None = None
    first_lines: list[str] = []
    errs: list[str] = []
    line_no = 0
    total_bytes = 0

    try:
        for raw in fh:
            line_no += 1
            total_bytes += len(raw)
            if out is not None:
                out.write(raw)
            try:
                s = raw.decode("ascii")
            except UnicodeDecodeError:
                _fail(errs, f"line {line_no}: non-ASCII bytes")
                malformed += 1
                continue
            s = s.strip()
            if not s:
                continue
            if len(first_lines) < 6:
                first_lines.append(s)
            tok = s.split()
            tag = tok[0]

            if tag == "c":
                continue
            if tag == "p":
                if header is not None:
                    _fail(errs, f"line {line_no}: duplicate problem line")
                    continue
                header = s
                if kind == "gr":
                    if len(tok) != 4 or tok[1] != "sp":
                        _fail(errs, f"line {line_no}: bad .gr header, expected 'p sp n m': {s!r}")
                    else:
                        n, m = int(tok[2]), int(tok[3])
                else:
                    if len(tok) != 5 or tok[1] != "aux" or tok[3] != "co":
                        _fail(errs, f"line {line_no}: bad .co header, expected 'p aux sp co n': {s!r}")
                    else:
                        n = int(tok[4])
                if n is not None:
                    if n <= 0:
                        _fail(errs, f"line {line_no}: non-positive n={n}")
                    present = bytearray(n + 1)
                continue

            if tag == "a" and kind == "gr":
                a_cnt += 1
                if len(tok) != 4:
                    _fail(errs, f"line {line_no}: expected 4 fields in arc line: {s!r}")
                    malformed += 1
                    continue
                try:
                    u, v, w = int(tok[1]), int(tok[2]), int(tok[3])
                except ValueError:
                    _fail(errs, f"line {line_no}: non-integer arc field: {s!r}")
                    malformed += 1
                    continue
                w_min = w if w_min is None else min(w_min, w)
                w_max = w if w_max is None else max(w_max, w)
                for x in (u, v):
                    min_id = x if min_id is None else min(min_id, x)
                    max_id = x if max_id is None else max(max_id, x)
                if present is None:
                    _fail(errs, f"line {line_no}: arc before problem line")
                    continue
                if 1 <= u <= n and 1 <= v <= n:
                    if not present[u]:
                        present[u] = 1
                    if not present[v]:
                        present[v] = 1
                else:
                    out_of_range += 1
                    _fail(errs, f"line {line_no}: node id outside [1,{n}]: ({u},{v})")
                continue

            if tag == "v" and kind == "co":
                v_cnt += 1
                if len(tok) != 4:
                    _fail(errs, f"line {line_no}: expected 4 fields in coord line: {s!r}")
                    malformed += 1
                    continue
                try:
                    node = int(tok[1])
                    float(tok[2])
                    float(tok[3])
                except ValueError:
                    _fail(errs, f"line {line_no}: non-numeric coord line: {s!r}")
                    malformed += 1
                    continue
                coords += 1
                min_id = node if min_id is None else min(min_id, node)
                max_id = node if max_id is None else max(max_id, node)
                if present is None:
                    _fail(errs, f"line {line_no}: coord before problem line")
                    continue
                if 1 <= node <= n:
                    if present[node]:
                        dup_nodes += 1
                    else:
                        present[node] = 1
                else:
                    out_of_range += 1
                    _fail(errs, f"line {line_no}: node id outside [1,{n}]: {node}")
                continue

            # Any other tag ('t', 'g', ...) is not part of these files.
            _fail(errs, f"line {line_no}: unexpected record type {tag!r}: {s!r}")
            malformed += 1
    finally:
        fh.close()
        if out is not None:
            out.close()

    missing = 0
    if present is not None and n is not None:
        missing = n - sum(present[1:]) if n <= 8_000_000 else -1

    # ---- hard checks -------------------------------------------------------
    if header is None:
        _fail(errs, "missing problem line ('p ...')")
    if n is None:
        _fail(errs, "problem line did not yield a node count")
    if kind == "gr":
        if m is None:
            _fail(errs, "problem line did not yield an arc count")
        elif a_cnt != m:
            _fail(errs, f"arc count mismatch: header m={m}, counted a-lines={a_cnt}")
        if max_id is not None and n is not None and max_id != n:
            _fail(errs, f"max node id {max_id} != n {n}")
        if a_cnt == 0:
            _fail(errs, "no arc lines found")
        if malformed:
            _fail(errs, f"{malformed} malformed/unexpected line(s)")
    else:
        if v_cnt != n:
            _fail(errs, f"coord count mismatch: header n={n}, counted v-lines={v_cnt}")
        if max_id is not None and n is not None and max_id != n:
            _fail(errs, f"max node id {max_id} != n {n}")
        if dup_nodes:
            _fail(errs, f"{dup_nodes} duplicate node id(s) in coord file")
        if malformed:
            _fail(errs, f"{malformed} malformed/unexpected line(s)")

    result = {
        "path": os.path.abspath(path),
        "kind": kind,
        "compressed": gz,
        "plain_bytes": total_bytes,
        "lines": line_no,
        "header": header,
        "n": n,
        "m": m,
        "arc_lines": a_cnt if kind == "gr" else None,
        "coord_lines": v_cnt if kind == "co" else None,
        "min_node_id": min_id,
        "max_node_id": max_id,
        "missing_nodes": missing,
        "duplicate_id_lines": dup_nodes,
        "out_of_range_ids": out_of_range,
        "malformed_lines": malformed,
        "weight_min": w_min if kind == "gr" else None,
        "weight_max": w_max if kind == "gr" else None,
        "first_lines": first_lines,
        "extracted_to": os.path.abspath(extract_to) if extract_to else None,
        "ok": not errs,
        "errors": errs,
    }
    return result


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Verify a DIMACS .gr/.co file (optionally gzipped).")
    ap.add_argument("path", help="input .gr / .co file, optionally .gz")
    ap.add_argument("--extract-to", default=None, help="write decompressed bytes here while validating")
    ap.add_argument("--json", nargs="?", const="-", default=None, metavar="FILE",
                    help="also write the JSON report to FILE ('-' for stdout)")
    ap.add_argument("--quiet", action="store_true", help="do not print the human-readable report")
    args = ap.parse_args(argv)

    try:
        res = verify(args.path, args.extract_to)
    except (OSError, EOFError, gzip.BadGzipFile) as exc:
        print(f"FAIL {args.path}: cannot read/decompress: {exc}", file=sys.stderr)
        return 2

    if not args.quiet:
        st = "OK  " if res["ok"] else "FAIL"
        print(f"{st} {res['path']}")
        print(f"     kind={res['kind']} plain_bytes={res['plain_bytes']} lines={res['lines']}")
        print(f"     header={res['header']!r}")
        if res["kind"] == "gr":
            print(f"     n={res['n']} m={res['m']} arc_lines={res['arc_lines']} "
                  f"weight=[{res['weight_min']},{res['weight_max']}]")
        else:
            print(f"     n={res['n']} coord_lines={res['coord_lines']}")
        print(f"     node_id=[{res['min_node_id']},{res['max_node_id']}] "
              f"missing={res['missing_nodes']} out_of_range={res['out_of_range_ids']} "
              f"malformed={res['malformed_lines']}")
        if res["extracted_to"]:
            print(f"     extracted_to={res['extracted_to']}")
        for e in res["errors"]:
            print(f"     ERROR: {e}")
    if args.json:
        text = json.dumps(res, ensure_ascii=False, indent=2)
        if args.json == "-":
            print(text)
        else:
            os.makedirs(os.path.dirname(os.path.abspath(args.json)) or ".", exist_ok=True)
            with open(args.json, "w", encoding="utf-8") as f:
                f.write(text + "\n")
    return 0 if res["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
