//
// Complexity-fitting Dijkstra benchmark.
//
// The ordinary `dij-bench-v2` sweeps the legacy (n, m) ladder, where m jumps in
// an irregular pattern; that makes Google Benchmark's complexity fit noisy
// (RMS ~130%). This program constructs two families of test data with a fixed
// relation between n and m, so the fitted curve is meaningful:
//
//   sparse: m = 4 * n            (fixed average out-degree 4)
//           expected ~O(n log n) for heap Dijkstra; the queue-based
//           Bellman-Ford family scans ~O(n) edges on these random graphs.
//
//   dense:  m = n * (n - 1) / 4  (quadratic number of arcs)
//           the O(m log n) = O(n^2 log n) heap cost is dominated by n^2.
//
// Both families are run for every implementation, and each benchmark calls
// state.SetComplexityN(n) so Google Benchmark reports a least-squares fit of
// the selected model plus its normalized RMS error, e.g.:
//
//   complexity/sparse/dijkstra_std_BigO     x.xx NlgN
//   complexity/sparse/dijkstra_std_RMS      xx %
//
// The model is pinned explicitly:
//   * sparse -> NlgN: Dijkstra is O(m log n) with m = Theta(n);
//   * dense  -> N^2:  the true O(m log n) = O(n^2 log n) is dominated by n^2.
// We do not use oAuto here: its least-squares residual is in absolute time and
// is therefore dominated by the largest n, which makes the selection unstable
// (e.g. sparse FibonacciHeap sometimes flips to N^2 with a meaningless ~0
// coefficient). The pinned fits are stable and directly comparable.
//
// The graph is built once per benchmark argument, outside the timed loop; every
// implementation in a family sees exactly the same graph (fixed seed).
//
// Run:
//   build/.../dij-bench-complexity --benchmark_out=complexity.json
//       --benchmark_out_format=json
//   build/.../dij-bench-complexity --benchmark_filter=complexity/sparse
//

#include "binary-heap.hpp"
#include "csr-graph.hpp"
#include "fibonacci-heap.hpp"
#include "general-bellman-ford.hpp"
#include "general-dijkstra.hpp"
#include "heap-adaptor.hpp"
#include "radix-heap.hpp"
#include "random-graph.hpp"
#include "segment-tree-heap.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace db = dijkstra_bench;

using Entry = std::pair<int64_t, uint32_t>;

constexpr uint32_t kGraphSeed = 42;
constexpr uint32_t kSource = 1;
constexpr int64_t kWeightMax = 200000;

enum class Family {
  sparse, // m = 4 n
  dense,  // m = n (n - 1) / 4
};

uint32_t EdgeCount(Family family, uint32_t n) {
  if (family == Family::sparse)
    return 4 * n;
  return static_cast<uint32_t>(static_cast<std::uint64_t>(n) * (n - 1) / 4);
}

const char *FamilyName(Family family) {
  return family == Family::sparse ? "sparse" : "dense";
}

// ---------------------------------------------------------------------------
// Cross-check: every implementation must agree with the SLF+LLL SPFA reference
// before any timing happens (same scheme as dij-bench-v2.cpp).
// ---------------------------------------------------------------------------

template <typename Heap>
bool CheckDijkstra(const db::Graph &graph, const std::vector<uint32_t> &sources,
                   const std::vector<std::vector<int64_t>> &want,
                   const char *name) {
  const uint32_t n = graph.num_vertices();
  Heap heap(n);
  std::vector<int64_t> got(static_cast<std::size_t>(n) + 1);
  for (std::size_t i = 0; i < sources.size(); ++i) {
    db::dijkstra(graph, sources[i], got, heap);
    for (uint32_t v = 1; v <= n; ++v) {
      if (got[v] != want[i][v]) {
        std::fprintf(stderr,
                     "dij-bench-complexity: %s mismatch at n=%u source=%u "
                     "vertex=%u (got %lld, expected %lld)\n",
                     name, n, sources[i], v, static_cast<long long>(got[v]),
                     static_cast<long long>(want[i][v]));
        return false;
      }
    }
  }
  return true;
}

bool CheckBellmanFord(const db::Graph &graph,
                      const std::vector<uint32_t> &sources,
                      const std::vector<std::vector<int64_t>> &want) {
  const uint32_t n = graph.num_vertices();
  std::queue<Entry> queue;
  std::vector<int64_t> got(static_cast<std::size_t>(n) + 1);
  for (std::size_t i = 0; i < sources.size(); ++i) {
    db::bellman_ford(graph, sources[i], got, queue);
    if (got != want[i]) {
      std::fprintf(stderr,
                   "dij-bench-complexity: FIFO bellman_ford mismatch at n=%u "
                   "source=%u\n",
                   n, sources[i]);
      return false;
    }
  }
  return true;
}

bool VerifyImplementations() {
  struct Shape {
    uint32_t n;
    Family family;
  };
  const Shape shapes[] = {{64, Family::sparse},
                          {512, Family::sparse},
                          {64, Family::dense},
                          {512, Family::dense}};

  std::deque<uint32_t> queue;
  for (const Shape &shape : shapes) {
    const uint32_t n = shape.n;
    const db::Graph graph =
        db::MakeRandomGraph(n, EdgeCount(shape.family, n), 1000, 1);
    const std::vector<uint32_t> sources = {1, n};

    std::vector<std::vector<int64_t>> want;
    want.reserve(sources.size());
    for (uint32_t source : sources) {
      std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
      db::spfa_slf_lll(graph, source, dis, queue);
      want.push_back(std::move(dis));
    }

    if (!CheckDijkstra<db::StdHeapAdaptor>(graph, sources, want, "std") ||
        !CheckDijkstra<db::BinaryHeapAdaptor>(graph, sources, want, "binary") ||
        !CheckDijkstra<db::SegmentTreeHeap>(graph, sources, want, "segt") ||
        !CheckDijkstra<db::FibonacciHeap>(graph, sources, want, "fib") ||
        !CheckDijkstra<db::RadixHeap>(graph, sources, want, "radix") ||
        !CheckBellmanFord(graph, sources, want)) {
      return false;
    }
#ifdef DIJKSTRA_BENCH_HAS_PBDS
    if (!CheckDijkstra<db::PbdsPairingHeapAdaptor>(graph, sources, want,
                                                   "pbds_pairing") ||
        !CheckDijkstra<db::PbdsThinHeapAdaptor>(graph, sources, want,
                                                "pbds_thin")) {
      return false;
    }
#endif
  }
  std::printf("dij-bench-complexity: cross-check passed\n");
  return true;
}

// ---------------------------------------------------------------------------
// Benchmarks: one timed run per iteration; graph construction is outside.
// ---------------------------------------------------------------------------

template <typename Heap>
void RunDijkstra(benchmark::State &state, Family family) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const db::Graph graph =
      db::MakeRandomGraph(n, EdgeCount(family, n), kWeightMax, kGraphSeed);
  Heap heap(n);
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);

  std::uint64_t checksum = 0;
  for (auto _ : state) {
    db::dijkstra(graph, kSource, dis, heap);
    checksum ^= static_cast<std::uint64_t>(dis[n]);
  }
  benchmark::DoNotOptimize(checksum);
  state.SetComplexityN(static_cast<int64_t>(n));
}

void RunBellmanFord(benchmark::State &state, Family family) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const db::Graph graph =
      db::MakeRandomGraph(n, EdgeCount(family, n), kWeightMax, kGraphSeed);
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
  std::queue<Entry> queue;

  std::uint64_t checksum = 0;
  for (auto _ : state) {
    db::bellman_ford(graph, kSource, dis, queue);
    checksum ^= static_cast<std::uint64_t>(dis[n]);
  }
  benchmark::DoNotOptimize(checksum);
  state.SetComplexityN(static_cast<int64_t>(n));
}

void RunBellmanFordSlfLll(benchmark::State &state, Family family) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const db::Graph graph =
      db::MakeRandomGraph(n, EdgeCount(family, n), kWeightMax, kGraphSeed);
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
  std::deque<uint32_t> queue;

  std::uint64_t checksum = 0;
  for (auto _ : state) {
    db::spfa_slf_lll(graph, kSource, dis, queue);
    checksum ^= static_cast<std::uint64_t>(dis[n]);
  }
  benchmark::DoNotOptimize(checksum);
  state.SetComplexityN(static_cast<int64_t>(n));
}

void RegisterFamily(Family family, int64_t first_n, int64_t last_n,
                    benchmark::BigO complexity) {
  using RunFn = void (*)(benchmark::State &, Family);
  struct Entry {
    const char *name;
    RunFn run;
  };
  const Entry entries[] = {
      {"dijkstra_std", &RunDijkstra<db::StdHeapAdaptor>},
      {"dijkstra_binary", &RunDijkstra<db::BinaryHeapAdaptor>},
      {"dijkstra_segt", &RunDijkstra<db::SegmentTreeHeap>},
      {"dijkstra_fib", &RunDijkstra<db::FibonacciHeap>},
      {"dijkstra_radix", &RunDijkstra<db::RadixHeap>},
#ifdef DIJKSTRA_BENCH_HAS_PBDS
      {"dijkstra_pbds_pairing", &RunDijkstra<db::PbdsPairingHeapAdaptor>},
      {"dijkstra_pbds_thin", &RunDijkstra<db::PbdsThinHeapAdaptor>},
#endif
      {"spfa", &RunBellmanFord},
      {"spfa_slf_lll", &RunBellmanFordSlfLll},
  };

  const std::string prefix =
      "complexity/" + std::string(FamilyName(family)) + "/";
  for (const Entry &entry : entries) {
    benchmark::RegisterBenchmark(
        prefix + entry.name,
        [run = entry.run, family](benchmark::State &state) {
          run(state, family);
        })
        ->RangeMultiplier(2)
        ->Range(first_n, last_n)
        ->Complexity(complexity);
  }
}

void RegisterBenchmarks() {
  // Sparse: n = 2^10 .. 2^17 with a fixed average out-degree of 4.
  // Expected for Dijkstra: O(m log n) = O(n log n).
  RegisterFamily(Family::sparse, 1 << 10, 1 << 17, benchmark::oNLogN);
  // Dense: n = 2^7 .. 2^12 with m = n (n - 1) / 4 arcs.
  // Expected: O(n^2 log n), dominated by n^2.
  RegisterFamily(Family::dense, 1 << 7, 1 << 12, benchmark::oNSquared);
}

} // namespace

int main(int argc, char **argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  if (!VerifyImplementations())
    return 1;
  RegisterBenchmarks();
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
