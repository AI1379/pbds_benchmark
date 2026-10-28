//
// DIMACS road-network Dijkstra benchmark.
//
// Loads a 9th-DIMACS-challenge `.gr` graph and a `.p2p` / `.ss` query file and
// times one full pass over the query set for every implementation. Each
// benchmark iteration runs all queries, so the reported time is
// `queries * average query time`; the `us_per_query` counter gives the average
// directly and `items_per_second` is the query throughput.
//
// Implementations (same set as dij-bench-v2.cpp):
//   dijkstra_std, dijkstra_binary, dijkstra_segt, dijkstra_fib,
//   dijkstra_radix, dijkstra_pbds_pairing, dijkstra_pbds_thin (GCC only),
//   spfa, spfa_slf_lll.
//
// Before timing, the first few queries are cross-checked against the SLF+LLL
// SPFA reference (spfa_slf_lll in general-bellman-ford.hpp); a mismatch aborts
// the run.
//
// Usage:
//   dij-bench-dimacs [--gr GRAPH.gr] [--queries QUERIES] [--limit N]
//                    [benchmark flags...]
//
// `--gr` defaults to $DIMACS_DATA_DIR/USA-road-d.NY.gr (configured by CMake);
// the query file defaults to the `.p2p` (then `.ss`) next to it in
// <dir>/queries/<stem>.p2p. `--limit N` only runs the first N queries, which is
// handy for big road networks.
//
// Examples:
//   dij-bench-dimacs --benchmark_filter=dimacs/USA-road-d.NY
//   dij-bench-dimacs --gr data/dimacs/USA-road-d.BAY.gr --limit 200
//   dij-bench-dimacs --gr data/dimacs/USA-road-d.NY.gr
//       --queries data/dimacs/queries/USA-road-d.NY.p2p
//       --benchmark_out=dimacs-NY.json --benchmark_out_format=json
//

#include "binary-heap.hpp"
#include "csr-graph.hpp"
#include "dimacs-parser.hpp"
#include "fibonacci-heap.hpp"
#include "general-bellman-ford.hpp"
#include "general-dijkstra.hpp"
#include "heap-adaptor.hpp"
#include "radix-heap.hpp"
#include "segment-tree-heap.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <exception>
#include <filesystem>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifndef DIMACS_DATA_DIR
#define DIMACS_DATA_DIR "data/dimacs"
#endif

namespace {

namespace db = dijkstra_bench;
namespace fs = std::filesystem;

using Entry = std::pair<int64_t, uint32_t>;

/// One timed query. `.ss` files only name a source (target stays 0 and the
/// final distance is used for the checksum); `.p2p` files also name a target.
struct Query {
  uint32_t source = 0;
  uint32_t target = 0;
};

struct BenchData {
  db::Graph graph;
  std::vector<Query> queries;
  std::string region;
};

struct Options {
  fs::path graph;
  fs::path queries;
  std::size_t limit = 0; ///< 0 = all queries
  bool help = false;
};

void print_usage(std::FILE *out) {
  std::fprintf(
      out,
      "Usage: dij-bench-dimacs [options] [benchmark flags]\n"
      "\n"
      "Benchmark Dijkstra implementations on a DIMACS road network.\n"
      "\n"
      "Options:\n"
      "  --gr <file.gr>        Graph file (default: $DIMACS_DATA_DIR/"
      "USA-road-d.NY.gr).\n"
      "  --queries <file>      `.p2p` or `.ss` query file (default: look for\n"
      "                        <graph-dir>/queries/<stem>.p2p, then .ss).\n"
      "  --limit <n>           Only run the first n queries (default: all).\n"
      "  -h, --help            Show this help text.\n"
      "\n"
      "Benchmark flags (e.g. --benchmark_filter=..., --benchmark_out=...) are\n"
      "passed through to Google Benchmark.\n");
}

/// Reads `--name value` or `--name=value`. Returns 1 if matched with a value
/// stored in `value`, 0 if not matched, -1 if matched but the value is missing.
int match_option(int &i, int argc, char **argv, std::string_view arg,
                 std::string_view name, std::string &value) {
  if (arg == name) {
    if (i + 1 >= argc)
      return -1;
    value = argv[++i];
    return 1;
  }
  const std::string prefix = std::string(name) + "=";
  if (arg.size() > prefix.size() &&
      arg.compare(0, prefix.size(), prefix) == 0) {
    value = std::string(arg.substr(prefix.size()));
    return 1;
  }
  return 0;
}

/// Parses our own options and copies everything else into `bench_argv` so it
/// can be handed to Google Benchmark. Returns false on a command-line error.
bool parse_args(int argc, char **argv, Options &options,
                std::vector<char *> &bench_argv) {
  options.graph = fs::path(DIMACS_DATA_DIR) / "USA-road-d.NY.gr";
  bench_argv.push_back(argv[0]);

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    std::string value;
    if (arg == "-h" || arg == "--help") {
      options.help = true;
      return true;
    }
    int matched = match_option(i, argc, argv, arg, "--gr", value);
    if (matched < 0) {
      std::fprintf(stderr, "dij-bench-dimacs: --gr needs a value\n");
      return false;
    }
    if (matched > 0) {
      options.graph = value;
      continue;
    }
    matched = match_option(i, argc, argv, arg, "--queries", value);
    if (matched < 0) {
      std::fprintf(stderr, "dij-bench-dimacs: --queries needs a value\n");
      return false;
    }
    if (matched > 0) {
      options.queries = value;
      continue;
    }
    matched = match_option(i, argc, argv, arg, "--limit", value);
    if (matched < 0) {
      std::fprintf(stderr, "dij-bench-dimacs: --limit needs a value\n");
      return false;
    }
    if (matched > 0) {
      try {
        options.limit = static_cast<std::size_t>(std::stoull(value));
      } catch (const std::exception &) {
        std::fprintf(stderr, "dij-bench-dimacs: invalid --limit '%s'\n",
                     value.c_str());
        return false;
      }
      continue;
    }
    bench_argv.push_back(argv[i]);
  }
  return true;
}

/// Finds the query file that goes with `graph`: the generated queries live in
/// <graph-dir>/queries/<stem>.p2p (or .ss); fall back to the graph directory.
fs::path default_query_file(const fs::path &graph) {
  const std::string stem = graph.stem().string();
  const fs::path candidates[] = {
      graph.parent_path() / "queries" / (stem + ".p2p"),
      graph.parent_path() / "queries" / (stem + ".ss"),
      graph.parent_path() / (stem + ".p2p"),
      graph.parent_path() / (stem + ".ss"),
  };
  for (const fs::path &candidate : candidates) {
    std::error_code ec;
    if (fs::exists(candidate, ec))
      return candidate;
  }
  return {};
}

BenchData load_data(const Options &options) {
  if (!fs::exists(options.graph)) {
    throw std::runtime_error("graph file not found: " + options.graph.string());
  }

  const auto parse_start = std::chrono::steady_clock::now();
  BenchData data;
  db::DimacsGraphInfo graph_info;
  data.graph = db::read_dimacs_gr(options.graph, {}, &graph_info);
  const double parse_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                    parse_start)
          .count();
  data.region = options.graph.stem().string();
  if (data.region.empty())
    data.region = "dimacs";

  const fs::path query_path = options.queries.empty()
                                  ? default_query_file(options.graph)
                                  : options.queries;
  if (query_path.empty()) {
    throw std::runtime_error("no query file given and none found next to " +
                             options.graph.string());
  }

  db::DimacsQueryInfo query_info;
  db::DimacsQueryFile query_file = db::read_dimacs_queries(
      query_path, data.graph.num_vertices(), {}, &query_info);
  if (query_file.is_single_source()) {
    for (uint32_t source : query_file.sources)
      data.queries.push_back({source, 0});
  } else {
    for (const db::DimacsQuery &pair : query_file.pairs)
      data.queries.push_back({pair.source, pair.target});
  }
  if (options.limit != 0 && data.queries.size() > options.limit)
    data.queries.resize(options.limit);
  if (data.queries.empty())
    throw std::runtime_error("query file contains no queries: " +
                             query_path.string());

  std::printf("DIMACS benchmark\n");
  std::printf("  graph   : %s\n", options.graph.string().c_str());
  std::printf("            n=%u m=%llu, weight range [%d, %d], parse %.3f s\n",
              data.graph.num_vertices(),
              static_cast<unsigned long long>(data.graph.num_edges()),
              graph_info.min_weight, graph_info.max_weight, parse_seconds);
  std::printf("  queries : %s\n", query_path.string().c_str());
  std::printf("            kind=%s, declared=%llu, using=%zu\n",
              query_info.kind == db::DimacsQueryKind::single_source
                  ? "single-source (.ss)"
                  : "point-to-point (.p2p)",
              static_cast<unsigned long long>(query_info.declared_queries),
              data.queries.size());
  std::printf("  region  : %s\n", data.region.c_str());
  return data;
}

// ---------------------------------------------------------------------------
// Cross-check: the first few queries are computed with the SLF+LLL SPFA
// reference and compared with every heap implementation (full dis array).
// ---------------------------------------------------------------------------

template <typename Heap>
bool check_heap_queries(const BenchData &data,
                        const std::vector<std::vector<int64_t>> &want,
                        const char *name) {
  const uint32_t n = data.graph.num_vertices();
  Heap heap(n);
  std::vector<int64_t> got(static_cast<std::size_t>(n) + 1);
  for (std::size_t i = 0; i < want.size(); ++i) {
    db::dijkstra(data.graph, data.queries[i].source, got, heap);
    for (uint32_t v = 1; v <= n; ++v) {
      if (got[v] != want[i][v]) {
        std::fprintf(stderr,
                     "dij-bench-dimacs: %s mismatch on query %zu (source=%u) "
                     "at vertex %u (got %lld, expected %lld)\n",
                     name, i, data.queries[i].source, v,
                     static_cast<long long>(got[v]),
                     static_cast<long long>(want[i][v]));
        return false;
      }
    }
  }
  return true;
}

bool verify_implementations(const BenchData &data) {
  const std::size_t count = std::min<std::size_t>(data.queries.size(), 3);
  std::vector<std::vector<int64_t>> want;
  want.reserve(count);
  std::deque<uint32_t> queue;
  for (std::size_t i = 0; i < count; ++i) {
    std::vector<int64_t> dis(
        static_cast<std::size_t>(data.graph.num_vertices()) + 1);
    db::spfa_slf_lll(data.graph, data.queries[i].source, dis, queue);
    want.push_back(std::move(dis));
  }

  const bool ok =
      check_heap_queries<db::StdHeapAdaptor>(data, want, "std") &&
      check_heap_queries<db::BinaryHeapAdaptor>(data, want, "binary") &&
      check_heap_queries<db::SegmentTreeHeap>(data, want, "segt") &&
      check_heap_queries<db::FibonacciHeap>(data, want, "fib") &&
      check_heap_queries<db::RadixHeap>(data, want, "radix")
#ifdef DIJKSTRA_BENCH_HAS_PBDS
      && check_heap_queries<db::PbdsPairingHeapAdaptor>(data, want,
                                                        "pbds_pairing") &&
      check_heap_queries<db::PbdsThinHeapAdaptor>(data, want, "pbds_thin")
#endif
      ;
  if (ok)
    std::printf("dij-bench-dimacs: cross-check passed on %zu queries\n", count);
  return ok;
}

// ---------------------------------------------------------------------------
// Benchmarks: one iteration = one pass over the whole query set.
// ---------------------------------------------------------------------------

template <typename Heap>
void run_dijkstra_benchmark(benchmark::State &state, const BenchData &data) {
  const uint32_t n = data.graph.num_vertices();
  Heap heap(n);
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);

  std::uint64_t checksum = 0;
  double us_per_query = 0.0;
  for (auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    for (const Query &query : data.queries) {
      db::dijkstra(data.graph, query.source, dis, heap);
      checksum ^=
          static_cast<std::uint64_t>(dis[query.target != 0 ? query.target : n]);
    }
    const auto stop = std::chrono::steady_clock::now();
    us_per_query =
        std::chrono::duration<double, std::micro>(stop - start).count() /
        static_cast<double>(data.queries.size());
  }
  benchmark::DoNotOptimize(checksum);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(data.queries.size()));
  state.counters["queries"] = static_cast<double>(data.queries.size());
  state.counters["us_per_query"] = us_per_query;
}

void run_spfa_benchmark(benchmark::State &state, const BenchData &data) {
  const uint32_t n = data.graph.num_vertices();
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
  std::queue<Entry> queue;

  std::uint64_t checksum = 0;
  double us_per_query = 0.0;
  for (auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    for (const Query &query : data.queries) {
      db::bellman_ford(data.graph, query.source, dis, queue);
      checksum ^=
          static_cast<std::uint64_t>(dis[query.target != 0 ? query.target : n]);
    }
    const auto stop = std::chrono::steady_clock::now();
    us_per_query =
        std::chrono::duration<double, std::micro>(stop - start).count() /
        static_cast<double>(data.queries.size());
  }
  benchmark::DoNotOptimize(checksum);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(data.queries.size()));
  state.counters["queries"] = static_cast<double>(data.queries.size());
  state.counters["us_per_query"] = us_per_query;
}

void run_spfa_slf_lll_benchmark(benchmark::State &state,
                                const BenchData &data) {
  const uint32_t n = data.graph.num_vertices();
  std::vector<int64_t> dis(static_cast<std::size_t>(n) + 1);
  std::deque<uint32_t> queue;

  std::uint64_t checksum = 0;
  double us_per_query = 0.0;
  for (auto _ : state) {
    const auto start = std::chrono::steady_clock::now();
    for (const Query &query : data.queries) {
      db::spfa_slf_lll(data.graph, query.source, dis, queue);
      checksum ^=
          static_cast<std::uint64_t>(dis[query.target != 0 ? query.target : n]);
    }
    const auto stop = std::chrono::steady_clock::now();
    us_per_query =
        std::chrono::duration<double, std::micro>(stop - start).count() /
        static_cast<double>(data.queries.size());
  }
  benchmark::DoNotOptimize(checksum);
  state.SetItemsProcessed(static_cast<int64_t>(state.iterations()) *
                          static_cast<int64_t>(data.queries.size()));
  state.counters["queries"] = static_cast<double>(data.queries.size());
  state.counters["us_per_query"] = us_per_query;
}

void register_benchmarks(const BenchData &data) {
  const std::string prefix = "dimacs/" + data.region + "/";

  benchmark::RegisterBenchmark(
      prefix + "dijkstra_std", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::StdHeapAdaptor>(state, data);
      });
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_binary", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::BinaryHeapAdaptor>(state, data);
      });
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_segt", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::SegmentTreeHeap>(state, data);
      });
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_fib", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::FibonacciHeap>(state, data);
      });
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_radix", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::RadixHeap>(state, data);
      });
#ifdef DIJKSTRA_BENCH_HAS_PBDS
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_pbds_pairing", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::PbdsPairingHeapAdaptor>(state, data);
      });
  benchmark::RegisterBenchmark(
      prefix + "dijkstra_pbds_thin", [&data](benchmark::State &state) {
        run_dijkstra_benchmark<db::PbdsThinHeapAdaptor>(state, data);
      });
#endif
  benchmark::RegisterBenchmark(
      prefix + "spfa",
      [&data](benchmark::State &state) { run_spfa_benchmark(state, data); });
  benchmark::RegisterBenchmark(prefix + "spfa_slf_lll",
                               [&data](benchmark::State &state) {
                                 run_spfa_slf_lll_benchmark(state, data);
                               });
}

} // namespace

int main(int argc, char **argv) {
  benchmark::MaybeReenterWithoutASLR(argc, argv);

  Options options;
  std::vector<char *> bench_argv;
  if (!parse_args(argc, argv, options, bench_argv)) {
    print_usage(stderr);
    return 2;
  }
  if (options.help) {
    print_usage(stdout);
    return 0;
  }

  try {
    const BenchData data = load_data(options);
    if (!verify_implementations(data))
      return 1;
    register_benchmarks(data);

    int bench_argc = static_cast<int>(bench_argv.size());
    benchmark::Initialize(&bench_argc, bench_argv.data());
    if (benchmark::ReportUnrecognizedArguments(bench_argc, bench_argv.data()))
      return 2;
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
  } catch (const std::exception &error) {
    std::fprintf(stderr, "dij-bench-dimacs: %s\n", error.what());
    return 1;
  }
  return 0;
}
