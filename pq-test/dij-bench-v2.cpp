//
// New-architecture Dijkstra benchmark.
//
// One CSR graph + one source is shared by every implementation (and built once
// per benchmark argument, outside the timed loop), so all heaps see exactly
// the same input. Implementations are the ones used by the legacy benchmark
// plus the Fibonacci heap and SPFA (the legacy single-file benchmark is named
// in parentheses):
//   - dijkstra_std: std::priority_queue adaptor (legacy dijkstra-stl.hpp)
//   - dijkstra_binary: hand-written binary heap (legacy dijkstra-heap.hpp)
//   - dijkstra_segt: segment tree heap (legacy dijkstra-segt.hpp)
//   - dijkstra_pbds_pairing / dijkstra_pbds_thin: pb_ds pairing / thin heap
//     (legacy dijkstra-pbds.hpp / dijkstra-pbds-thin-heap.hpp)
//   - dijkstra_fib: Fibonacci heap (pq-test/fibonacci-heap.hpp)
//   - dijkstra_radix: radix heap (pq-test/radix-heap.hpp); requires monotone
//     insertion (every push key >= the last popped key), which Dijkstra
//     preserves because edge weights are non-negative
//   - spfa: FIFO queue-based Bellman-Ford (legacy spfa-stl.hpp)
//   - spfa_slf_lll: SLF+LLL deque ordering (also the cross-check reference)
//
// The old single-file benchmarks in legacy-benchmark/ are kept untouched for
// historical results; this program is the one to use for new measurements.
//
// Before timing anything, main() cross-checks every implementation against the
// SLF+LLL SPFA reference (spfa_slf_lll in general-bellman-ford.hpp) on a set of
// random graphs and sources. If they disagree the program exits without
// running benchmarks, so a broken heap cannot silently produce garbage timings.
//
// Build (Release! assert and benchmarks do not mix):
//   cmake --build build --target dij-bench-v2
// Run a subset / save JSON for plots:
//   build/.../dij-bench-v2 --benchmark_filter='dijkstra_(std|binary)'
//   build/.../dij-bench-v2 --benchmark_out=dij-v2.json
//
// Note: the legacy benchmark generated a fresh graph inside the paused part of
// every iteration; the layout/random generator here differs slightly, so the
// absolute numbers are new measurements, not a continuation of the 2022 table.
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
#include <utility>
#include <vector>

namespace {

namespace db = dijkstra_bench;

using Entry = std::pair<int64_t, uint32_t>;

constexpr uint32_t kGraphSeed = 42;
constexpr uint32_t kSource = 1;
constexpr int64_t kWeightMax = 200000; // same as the legacy benchmark

// Same argument sweep as the legacy benchmark: (n, m, wmax). The dense end is
// clamped at m = 500000 like the old dij-bench.
void DijkstraArguments(benchmark::Benchmark *b) {
  for (int64_t i = 10; i < 200000;) {
    for (int64_t j = i - 1; j < (i * (i - 1) / 2) && j < 500000;) {
      b->Args({i, j, kWeightMax});
      if (j < 10000) {
        j <<= 3;
      } else {
        j <<= 1;
      }
    }
    if (i <= 10000) {
      i <<= 4;
    } else {
      i <<= 1;
    }
  }
}

// ---------------------------------------------------------------------------
// Cross-check: every implementation must agree with the SLF+LLL SPFA reference
// before we time anything. Runs each heap on several sources with the same heap
// object, which also verifies the "heap is drained, so it is reusable without
// clear()" assumption the benchmark loop relies on.
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
                     "dij-bench-v2: %s mismatch at n=%u source=%u vertex=%u "
                     "(got %lld, expected %lld)\n",
                     name, n, sources[i], v, static_cast<long long>(got[v]),
                     static_cast<long long>(want[i][v]));
        return false;
      }
    }
  }
  return true;
}

bool VerifyImplementations() {
  // n == 1 and tiny graphs catch off-by-one/empty-heap mistakes; the larger
  // ones exercise the reuse path with more than a handful of pops.
  const std::pair<uint32_t, uint32_t> shapes[] = {
      {1, 0},   {2, 3},    {3, 5},      {5, 12},
      {10, 30}, {30, 200}, {100, 1000}, {500, 4000}};

  std::deque<uint32_t> queue;
  for (const auto &[n, m] : shapes) {
    for (uint32_t seed : {1u, 42u, 20261027u}) {
      const db::Graph graph = db::MakeRandomGraph(n, m, 1000, seed);
      const std::vector<uint32_t> sources = {1, n};

      std::vector<std::vector<int64_t>> want;
      for (uint32_t source : sources) {
        std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
        db::spfa_slf_lll(graph, source, dis, queue);
        want.push_back(std::move(dis));
      }

      if (!CheckDijkstra<db::StdHeapAdaptor>(graph, sources, want, "std") ||
          !CheckDijkstra<db::BinaryHeapAdaptor>(graph, sources, want,
                                                "binary") ||
          !CheckDijkstra<db::SegmentTreeHeap>(graph, sources, want, "segt") ||
          !CheckDijkstra<db::FibonacciHeap>(graph, sources, want, "fib") ||
          !CheckDijkstra<db::RadixHeap>(graph, sources, want, "radix")) {
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
  }
  std::printf("dij-bench-v2: cross-check passed\n");
  return true;
}

// ---------------------------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------------------------

template <typename Heap> void RunDijkstraBenchmark(benchmark::State &state) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const auto m = static_cast<uint32_t>(state.range(1));
  const auto wmax = static_cast<uint32_t>(state.range(2));
  // Graph construction is not part of any measured run.
  const db::Graph graph = db::MakeRandomGraph(n, m, wmax, kGraphSeed);
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
  Heap heap(n);

  std::uint64_t checksum = 0;
  for (auto _ : state) {
    db::dijkstra(graph, kSource, dis, heap);
    checksum ^= static_cast<std::uint64_t>(dis[n]);
  }
  benchmark::DoNotOptimize(checksum);
  state.SetComplexityN(static_cast<int64_t>(n));
}

void RunSpfaBenchmark(benchmark::State &state) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const auto m = static_cast<uint32_t>(state.range(1));
  const auto wmax = static_cast<uint32_t>(state.range(2));
  const db::Graph graph = db::MakeRandomGraph(n, m, wmax, kGraphSeed);
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

void RunSpfaSlfLllBenchmark(benchmark::State &state) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const auto m = static_cast<uint32_t>(state.range(1));
  const auto wmax = static_cast<uint32_t>(state.range(2));
  const db::Graph graph = db::MakeRandomGraph(n, m, wmax, kGraphSeed);
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

} // namespace

#define BENCHMARK_DIJKSTRA(tag, HeapType)                                      \
  static void bench_dijkstra_##tag(benchmark::State &state) {                  \
    RunDijkstraBenchmark<HeapType>(state);                                     \
  }                                                                            \
  BENCHMARK(bench_dijkstra_##tag)                                              \
      ->Apply(DijkstraArguments)                                               \
      ->Complexity(benchmark::oNLogN)

BENCHMARK_DIJKSTRA(std, db::StdHeapAdaptor);
BENCHMARK_DIJKSTRA(binary, db::BinaryHeapAdaptor);
BENCHMARK_DIJKSTRA(segt, db::SegmentTreeHeap);
BENCHMARK_DIJKSTRA(fib, db::FibonacciHeap);
BENCHMARK_DIJKSTRA(radix, db::RadixHeap);
#ifdef DIJKSTRA_BENCH_HAS_PBDS
BENCHMARK_DIJKSTRA(pbds_pairing, db::PbdsPairingHeapAdaptor);
BENCHMARK_DIJKSTRA(pbds_thin, db::PbdsThinHeapAdaptor);
#endif

static void bench_spfa(benchmark::State &state) { RunSpfaBenchmark(state); }
BENCHMARK(bench_spfa)->Apply(DijkstraArguments)->Complexity(benchmark::oNLogN);

static void bench_spfa_slf_lll(benchmark::State &state) {
  RunSpfaSlfLllBenchmark(state);
}
BENCHMARK(bench_spfa_slf_lll)
    ->Apply(DijkstraArguments)
    ->Complexity(benchmark::oNLogN);

int main(int argc, char **argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);
  if (!VerifyImplementations())
    return 1;
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return 0;
}
