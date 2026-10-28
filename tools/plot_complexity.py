"""Plot fixed-density scaling with sample SD, and normalized CPU costs."""
import math
from benchmark_plot_common import *


def main():
    args = parser(__doc__).parse_args()
    setup_style()
    points = [p for p in load_points(args.data_dir) if p.suite == "complexity"]
    for family in ("sparse", "dense"):
        subset = [p for p in points if p.scenario == family]
        fig, ax = plt.subplots(figsize=(8.4, 4.8))
        draw_series(ax, subset, lambda p: p.n, lambda p: p.cpu_ms, lambda p: p.cpu_sd_ms)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("Vertices n")
        ax.set_ylabel("CPU time per run / ms")
        ax.set_title(("Sparse: m = 4n" if family == "sparse" else "Dense: m = n(n-1)/4") + "; mean +/- sample SD, 3 repetitions")
        legend_below(fig, ax, y=-0.03)
        fig.tight_layout(rect=(0, 0.12, 1, 1))
        save(fig, args.output_dir, f"complexity-{family}")
    fig, axes = plt.subplots(1, 2, figsize=(10, 4.7))
    for ax, family in zip(axes, ("sparse", "dense")):
        subset = [p for p in points if p.scenario == family]
        divisor = (lambda p: p.n * math.log2(p.n)) if family == "sparse" else (lambda p: p.n ** 2)
        draw_series(ax, subset, lambda p: p.n, lambda p: p.cpu_ms * 1e6 / divisor(p))
        ax.set_xscale("log", base=2)
        ax.set_xlabel("Vertices n")
        ax.set_ylabel("ns / (n log2 n)" if family == "sparse" else "ns / n^2")
        ax.set_title(f"{family.capitalize()} normalized CPU cost")
    legend_below(fig, axes[0], y=-0.04)
    fig.tight_layout(rect=(0, 0.14, 1, 1))
    save(fig, args.output_dir, "complexity-normalized")


if __name__ == "__main__":
    main()
