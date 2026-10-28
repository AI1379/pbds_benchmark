"""Plot all v2 cases in fixed-n panels; do not join the irregular n/m sweep."""
from benchmark_plot_common import *


def main():
    args = parser(__doc__).parse_args()
    setup_style()
    points = [p for p in load_points(args.data_dir) if p.suite == "dij-v2"]
    sizes = sorted({p.n for p in points})
    if len(sizes) != 6:
        raise ValueError("Expected the six registered vertex sizes")
    diagnostic = args.data_dir / "random-reachability.csv"
    reachability = read_csv(diagnostic) if diagnostic.exists() else []
    fig, axes = plt.subplots(2, 3, figsize=(11.5, 7.2))
    for ax, n in zip(axes.flat, sizes):
        draw_series(ax, [p for p in points if p.n == n], lambda p: p.m,
                    lambda p: p.cpu_ms, lambda p: p.cpu_sd_ms)
        ax.set_xscale("log", base=2)
        ax.set_yscale("log")
        ax.set_xlabel("Arcs m")
        ax.set_ylabel("CPU time / ms")
        checks = [r for r in reachability if r["suite"] == "dij-v2" and int(r["n"]) == n]
        isolated = bool(checks) and all(int(r["reachable_arcs"]) == 0 for r in checks)
        ax.set_title(f"n = {n:,}" + ("\nIsolated source (0 arcs visited)" if isolated else ""), fontsize=10)
    legend_below(fig, axes.flat[0], y=-0.015)
    fig.suptitle("Random graphs: all cases; mean +/- sample SD (3 repetitions)", fontsize=12)
    fig.tight_layout(rect=(0, 0.095, 1, 0.96))
    save(fig, args.output_dir, "random-fixed-n")


if __name__ == "__main__":
    main()
