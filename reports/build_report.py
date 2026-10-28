"""Compile main.tex twice with XeLaTeX and copy the finished PDF to report.pdf."""
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent


def main():
    engine = shutil.which("xelatex")
    if not engine:
        raise SystemExit("XeLaTeX is required (TeX Live with ctex and Fandol fonts).")
    output = ROOT / "build"
    output.mkdir(exist_ok=True)
    for number in (1, 2):
        result = subprocess.run([engine, "-interaction=nonstopmode", "-halt-on-error",
                                 "-file-line-error", f"-output-directory={output}", "main.tex"],
                                cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        log = output / f"compile-pass-{number}.txt"
        log.write_bytes(result.stdout)
        if result.returncode:
            print(result.stdout.decode("utf-8", errors="replace")[-6000:])
            raise SystemExit(f"XeLaTeX failed; inspect {log}")
    shutil.copy2(output / "main.pdf", ROOT / "report.pdf")
    print(f"Created {ROOT / 'report.pdf'}")


if __name__ == "__main__":
    main()
