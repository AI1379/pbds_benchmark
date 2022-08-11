#include <benchmark/benchmark.h>
#include <bits/stdc++.h>

#include "./dijkstra-heap.hpp"
#include "./dijkstra-pbds-thin-heap.hpp"
#include "./dijkstra-pbds.hpp"
#include "./dijkstra-segt.hpp"
#include "./dijkstra-stl.hpp"
#include "./graph-generator.hpp"
#include "./spfa-stl.hpp"

#define INIT_DIJKSTRA(nsp)                                                     \
  nsp::graph = GraphGenerator::graph;                                          \
  nsp::n = GraphGenerator::graph_n;                                            \
  nsp::m = GraphGenerator::graph_m;                                            \
  nsp::s = 1
#define OUTPUT_DIJKSTRA(nsp)                                                   \
  for (int i = 1; i <= nsp::n; i++) {                                          \
    cout << nsp::dis[i] << ' ';                                                \
  }                                                                            \
  cout << endl
#define RUN_DIJKSTRA(nsp)                                                      \
  INIT_DIJKSTRA(nsp);                                                          \
  nsp::dijkstra();                                                             \
  OUTPUT_DIJKSTRA(nsp)

static void dijkstraArguments(benchmark::internal::Benchmark *b) {
  for (ll i = 10; i < 200000;) {
    for (ll j = i - 1; j < (i * (i - 1) / 2) && j < 500000;) {
      b->Args({i, j, 200000});
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

#define ADD_BENCHMARK(nsp)                                                     \
  static void bench_##nsp(benchmark::State &state) {                           \
    auto n = state.range(0);                                                   \
    auto m = state.range(1);                                                   \
    auto w = state.range(2);                                                   \
    for (auto _ : state) {                                                     \
      state.PauseTiming();                                                     \
      GraphGenerator::clearGraph();                                            \
      GraphGenerator::randomGraph(n, m, make_pair(1, w), true);                \
      INIT_DIJKSTRA(nsp);                                                      \
      state.ResumeTiming();                                                    \
      nsp::dijkstra();                                                         \
    }                                                                          \
    state.SetComplexityN(n);                                                   \
  }                                                                            \
  BENCHMARK(bench_##nsp)                                                       \
      ->Apply(dijkstraArguments)                                               \
      ->Complexity(benchmark::oNLogN)

using namespace std;

ADD_BENCHMARK(spfa_stl);
ADD_BENCHMARK(dijkstra_stl);
ADD_BENCHMARK(dijkstra_pbds);
ADD_BENCHMARK(dijkstra_pbds_thin_heap);
ADD_BENCHMARK(dijkstra_heap);
ADD_BENCHMARK(dijkstra_segt);

BENCHMARK_MAIN();
