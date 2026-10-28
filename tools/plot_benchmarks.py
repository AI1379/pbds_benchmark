"""One-command entry point: generate all figures and report data tables."""
from pathlib import Path
import subprocess
import sys
from benchmark_plot_common import PROJECT, parser


def main():
    args = parser(__doc__).parse_args()
    here = Path(__file__).resolve().parent
    for script in ("plot_dimacs.py", "plot_complexity.py", "plot_random.py"):
        subprocess.run([sys.executable, str(here / script), "--data-dir", str(args.data_dir),
                        "--output-dir", str(args.output_dir)], check=True)
    subprocess.run([sys.executable, str(here / "generate_report_tables.py"),
                    "--data-dir", str(args.data_dir), "--output-dir", str(PROJECT / "reports/tables")], check=True)


if __name__ == "__main__":
    main()
