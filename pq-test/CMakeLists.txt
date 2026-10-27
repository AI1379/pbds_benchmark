# ---------------------------------------------------------------------------
# pb_ds benchmark programs (sources live in legacy-benchmark/, built only when
# PBDS_BENCHMARKS is enabled)
#
# All of these targets include <ext/pb_ds>, which non-GCC compilers do not
# provide, so they cannot be added unconditionally. The other tests (DIMACS
# parser, heap adaptor contract tests) do not depend on pb_ds; see below.
# ---------------------------------------------------------------------------
if(PBDS_BENCHMARKS)
  pbds_add_benchmark(dij-bench legacy-benchmark/dij-bench.cpp)
  pbds_add_benchmark(dij-bench-hack-spfa legacy-benchmark/dij-bench-hack-spfa.cpp)

  # Functional test for the graph generator (not a benchmark; it has its own
  # main; the tree generator uses pb_ds as well).
  add_executable(graph-gen-test legacy-benchmark/graph-gen-test.cpp)
  if(MSVC)
    target_compile_options(graph-gen-test PRIVATE /W4 /utf-8)
  else()
    target_compile_options(graph-gen-test PRIVATE -Wall -Wextra)
  endif()
endif()

# ---------------------------------------------------------------------------
# DIMACS shortest-path dataset parser (dimacs-parser.hpp)
#
#   dimacs-inspect     -- CLI: parse .gr/.co/.ss/.p2p and print statistics
#   dimacs-parser-test -- unit tests (GoogleTest) + optional real-data tests
#
# Real-data tests use DIMACS_DATA_DIR pointing at <proj2>/data/dimacs/; if the
# data has not been downloaded the tests are skipped automatically (GTEST_SKIP),
# so a default build does not require network access.
# Neither target depends on pb_ds; both build with any compiler.
# ---------------------------------------------------------------------------
add_executable(dimacs-inspect dimacs-inspect.cpp)

add_executable(dimacs-parser-test dimacs-parser-test.cpp)
target_link_libraries(dimacs-parser-test PRIVATE gtest_main)
target_compile_definitions(dimacs-parser-test PRIVATE
  DIMACS_DATA_DIR="${PROJECT_SOURCE_DIR}/data/dimacs")

# ---------------------------------------------------------------------------
# Heap adaptor unit tests (GoogleTest)
#
# Covers the shared contract of the StdHeapAdaptor / SegmentTreeHeap / pb_ds
# heaps: model differential test (tied keys, lazy-deletion semantics, clear-
# then-reuse), randomized Dijkstra vs. Bellman-Ford, and SegmentTreeHeap
# regressions (1-based vertices, the v == n sentinel leaf, the early-exit bug
# with tied keys). The pb_ds heap cases are gated by the
# DIJKSTRA_BENCH_HAS_PBDS macro (they only exist for compilers with pb_ds).
# The assert-based death tests only take effect in builds without NDEBUG
# (Debug).
# ---------------------------------------------------------------------------
add_executable(heap-adaptor-test heap-adaptor-test.cpp)
target_link_libraries(heap-adaptor-test PRIVATE gtest_main)

foreach(_target dimacs-inspect dimacs-parser-test heap-adaptor-test)
  if(MSVC)
    target_compile_options(${_target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${_target} PRIVATE -Wall -Wextra)
  endif()
endforeach()

# ---------------------------------------------------------------------------
# GoogleTest per-case registration
#
# Every gtest case becomes a separate ctest test (named "proj2.Suite.Case"), so
# `ctest -R somecase` and the VS Code Testing panel can run/debug each case
# individually. TEST_PREFIX groups all proj2 tests under "proj2"; combined with
# the workspace setting "cmake.ctest.testSuiteDelimiter": "\\." the CMake Tools
# CTest view shows
#   proj2 -> Suite -> Case
# matching proj1's "proj1.*" prefix.
#
# Note: discovery runs the test binary with --gtest_list_tests at build time
# (POST_BUILD); MinGW binaries require C:\msys64\ucrt64\bin on the build
# environment's PATH.
# ---------------------------------------------------------------------------
include(GoogleTest)
gtest_discover_tests(dimacs-parser-test
  TEST_PREFIX "proj2."
  DISCOVERY_TIMEOUT 30)
gtest_discover_tests(heap-adaptor-test
  TEST_PREFIX "proj2."
  DISCOVERY_TIMEOUT 30)
