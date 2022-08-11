#include "./cc-hash.hpp"
#include "./gp-hash.hpp"
#include "./randomgen.hpp"
#include "./stl-map.hpp"
#include "./stl-unordered-map.hpp"
#include <benchmark/benchmark.h>

#define ADD_BENCHMARK(nsp)                           \
  static void bench_##nsp(benchmark::State &state) { \
    auto size = state.range(0);                      \
    auto len = state.range(1);                       \
    nsp::init(size, len);                            \
    for (auto _ : state) {                           \
      nsp::addRandomString();                        \
      nsp::getRandomString();                        \
      nsp::addRandomString();                        \
      nsp::getRandomString();                        \
      nsp::addRandomString();                        \
      nsp::getRandomString();                        \
      nsp::addRandomString();                        \
      nsp::getRandomString();                        \
    }                                                \
  }                                                  \
  BENCHMARK(bench_##nsp)->RangeMultiplier(8)->Ranges({{8, 200000}, {10, 6144}})

ADD_BENCHMARK(GPHash);
ADD_BENCHMARK(CCHash);
ADD_BENCHMARK(STLHash);
ADD_BENCHMARK(STLMap);

BENCHMARK_MAIN();