"""Shared CSV validation, statistics and styling for the proj2 report plots.

Only iteration rows are samples. Aggregate rows (including CV and RMS) must
never be treated as additional timings. All timings below use CPU milliseconds
except the explicitly named DIMACS wall-clock per-query plots.
"""
from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from statistics import fmean, stdev

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

PROJECT = Path(__file__).resolve().parents[1]
ALGORITHMS = (
    "dijkstra_std", "dijkstra_binary", "dijkstra_segt", "dijkstra_fib",
    "dijkstra_radix", "dijkstra_pbds_pairing", "dijkstra_pbds_thin",
    "spfa", "spfa_slf_lll",
)
LABELS = dict(zip(ALGORITHMS, (
    "std PQ", "Binary heap", "Segment tree", "Fibonacci heap", "Radix heap",
    "PBDS pairing", "PBDS thin", "FIFO SPFA", "SLF+LLL SPFA",
)))
COLORS = dict(zip(ALGORITHMS, (
    "#4C78A8", "#F58518", "#8C6D31", "#B279A2", "#009E73",
    "#E45756", "#72B7B2", "#666666", "#0072B2",
)))
MARKERS = dict(zip(ALGORITHMS, ("o", "s", "^", "D", "P", "v", "X", "<", ">")))
SCALE = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1000.0}


@dataclass(frozen=True)
class Point:
    suite: str
    scenario: str
    algorithm: str
    n: int
    m: int
    cpu_ms: float
    cpu_sd_ms: float | None
    wall_ms: float
    wall_sd_ms: float | None
    repetitions: int
    queries: int


def parser(description: str) -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description=description)
    p.add_argument("--data-dir", type=Path, default=PROJECT / "reports/data",
                   help="Directory containing all-results.csv and complexity-fits.csv")
    p.add_argument("--output-dir", type=Path, default=PROJECT / "reports/figures")
    return p


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def load_points(data_dir: Path) -> list[Point]:
    """Recompute means/sample SD from repetitions, checking complete coverage."""
    groups = defaultdict(list)
    for row in read_csv(data_dir / "all-results.csv"):
        if row["run_type"] != "iteration":
            continue
        if row.get("error_occurred", "").lower() in {"true", "1"}:
            raise ValueError(f"Failed benchmark: {row['name']}")
        algorithm = row["implementation"]
        if algorithm not in ALGORITHMS:
            raise ValueError(f"Unknown implementation: {algorithm}")
        key = (row["suite"], row["scenario"], algorithm, int(row["n"]), int(row["m"]))
        groups[key].append(row)
    points = []
    for (suite, scenario, algorithm, n, m), rows in sorted(groups.items()):
        expected = int(rows[0]["repetitions"])
        indices = {int(r["repetition_index"]) for r in rows}
        if len(rows) != expected or indices != set(range(expected)):
            raise ValueError(f"Missing or duplicate repetitions: {(suite, algorithm, n, m)}")
        queries = int(float(rows[0].get("queries") or 0))
        cpu = [float(r["cpu_time"]) * SCALE[r["time_unit"]] for r in rows]
        if suite.startswith("dimacs"):
            if queries != 1000 or any(int(r["iterations"]) != 1 for r in rows):
                raise ValueError("Expected one complete 1000-query batch")
            cpu = [t / queries for t in cpu]
            wall = [float(r["us_per_query"]) / 1000 for r in rows]
        else:
            wall = [float(r["real_time"]) * SCALE[r["time_unit"]] for r in rows]
        points.append(Point(suite, scenario, algorithm, n, m,
                            fmean(cpu), stdev(cpu) if len(cpu) > 1 else None,
                            fmean(wall), stdev(wall) if len(wall) > 1 else None,
                            len(rows), queries))
    by_case = defaultdict(set)
    for p in points:
        by_case[(p.suite, p.scenario, p.n, p.m)].add(p.algorithm)
    for case, implementations in by_case.items():
        if implementations != set(ALGORITHMS):
            raise ValueError(f"Incomplete implementation set: {case}")
    if not points:
        raise ValueError("No timing samples found")
    return points


def setup_style() -> None:
    plt.rcParams.update({
        "font.family": "DejaVu Sans", "font.size": 10, "axes.titlesize": 11,
        "axes.labelsize": 10, "legend.fontsize": 9, "xtick.labelsize": 9,
        "ytick.labelsize": 9, "axes.spines.top": False, "axes.spines.right": False,
        "pdf.fonttype": 42, "ps.fonttype": 42, "savefig.dpi": 180,
        "axes.axisbelow": True, "grid.alpha": 0.25,
    })


def save(fig, output_dir: Path, stem: str) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    for extension in ("pdf", "png"):
        metadata = {"Creator": "proj2 benchmark plotting scripts", "Author": ""} if extension == "pdf" else None
        fig.savefig(output_dir / f"{stem}.{extension}", bbox_inches="tight", metadata=metadata)
    plt.close(fig)
    print(f"Generated {stem}.pdf and {stem}.png")


def draw_series(ax, points: list[Point], xvalue, yvalue, sdvalue=None) -> None:
    for algorithm in ALGORITHMS:
        series = sorted((p for p in points if p.algorithm == algorithm), key=xvalue)
        if not series:
            continue
        xs, ys = [xvalue(p) for p in series], [yvalue(p) for p in series]
        ax.plot(xs, ys, label=LABELS[algorithm], color=COLORS[algorithm],
                marker=MARKERS[algorithm], markersize=4, linewidth=1.4,
                linestyle="--" if algorithm.startswith("spfa") else "-")
        if sdvalue:
            errors = [sdvalue(p) or 0 for p in series]
            ax.errorbar(xs, ys, yerr=errors, fmt="none", ecolor=COLORS[algorithm],
                        alpha=0.65, capsize=2, linewidth=0.7)
    ax.grid(True, which="major")


def legend_below(fig, ax, ncol=3, y=-0.02) -> None:
    handles, labels = ax.get_legend_handles_labels()
    fig.legend(handles, labels, loc="lower center", ncol=ncol,
               bbox_to_anchor=(0.5, y), frameon=False)
