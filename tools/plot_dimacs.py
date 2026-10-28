"""Plot NY/BAY mean per-query wall time and speedup relative to std PQ."""
from benchmark_plot_common import *


def main():
    args = parser(__doc__).parse_args()
    setup_style()
    points = [p for p in load_points(args.data_dir) if p.suite.startswith("dimacs")]
    regions = sorted({p.scenario for p in points}, reverse=True)
    if len(regions) != 2:
        raise ValueError("Expected both NY and BAY")
    for speedup in (False, True):
        fig, axes = plt.subplots(1, 2, figsize=(9.5, 5.0), sharey=True)
        for ax, region in zip(axes, regions):
            lookup = {p.algorithm: p for p in points if p.scenario == region}
            baseline = lookup["dijkstra_std"].wall_ms
            values = [baseline / lookup[a].wall_ms if speedup else lookup[a].wall_ms for a in ALGORITHMS]
            positions = list(range(len(ALGORITHMS)))
            ax.barh(positions, values, color=[COLORS[a] for a in ALGORITHMS], height=0.68)
            ax.set_yticks(positions, [LABELS[a] for a in ALGORITHMS])
            ax.invert_yaxis() if not ax.yaxis_inverted() else None
            ax.set_xscale("log")
            ax.set_xlim(min(values) * 0.55, max(values) * 2.4)
            for y, value in zip(positions, values):
                ax.annotate(f"{value:.2f}" + ("x" if speedup else ""), (value, y),
                            xytext=(4, 0), textcoords="offset points", va="center", fontsize=9)
            if speedup:
                ax.axvline(1, color="black", linestyle=":", linewidth=1)
            ax.grid(axis="x", which="major")
            ax.set_title(region.removeprefix("USA-road-d.") + " / 1000 queries")
            ax.set_xlabel("Speedup vs std PQ (log scale)" if speedup else "Wall time per query / ms (log scale)")
        fig.tight_layout()
        save(fig, args.output_dir, "dimacs-speedup" if speedup else "dimacs-runtime")


if __name__ == "__main__":
    main()
