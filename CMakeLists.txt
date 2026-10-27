cmake_minimum_required(VERSION 3.16)

project(pbds_benchmark LANGUAGES CXX)

# Generate compile_commands.json for clangd / IDE (single-config only)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# C++ standard for these sources (-std=c++20, same as .vscode/tasks.json)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# vendored: GoogleTest (googletest + googlemock), see third_party/README.md
# ---------------------------------------------------------------------------
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# vendored: Google Benchmark, see third_party/README.md
# ---------------------------------------------------------------------------
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/benchmark EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# Shared helper functions
# ---------------------------------------------------------------------------
# Add an executable target that uses benchmark::benchmark_main (the source
# already contains BENCHMARK_MAIN())
function(pbds_add_benchmark target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} PRIVATE benchmark::benchmark_main)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra)
  endif()
endfunction()

enable_testing()

# ---------------------------------------------------------------------------
# <ext/pb_ds> is only provided by GCC. It only really affects:
#   * the DIJKSTRA_BENCH_HAS_PBDS macro (pb_ds heap cases in tests)
#   * the pbds benchmark programs under hash-test/ and pq-test/legacy-benchmark/
#
# Tests that do not depend on pb_ds (dimacs-parser-test, the remaining
# heap-adaptor-test cases, dimacs-inspect) must build with any compiler, so the
# pq-test directory is always added (never skipped for lack of pb_ds).
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(PBDS_AVAILABLE ON)
else()
  set(PBDS_AVAILABLE OFF)
endif()

# Define the macro when pb_ds is available, so the code no longer needs
# __GNUC__ checks.
if(PBDS_AVAILABLE)
  add_compile_definitions(DIJKSTRA_BENCH_HAS_PBDS=1)
endif()

option(PBDS_BENCHMARKS
  "Build benchmarks that depend on GCC's <ext/pb_ds> (hash-test, pq-test/legacy-benchmark)"
  ${PBDS_AVAILABLE})

if(PBDS_BENCHMARKS AND NOT PBDS_AVAILABLE)
  message(WARNING
    "PBDS_BENCHMARKS=ON but the compiler is ${CMAKE_CXX_COMPILER_ID} (not GCC); "
    "<ext/pb_ds> benchmarks are unlikely to compile")
endif()

if(PBDS_BENCHMARKS)
  message(STATUS
    "Building pbds-based benchmarks (hash-test, pq-test/legacy-benchmark) "
    "with ${CMAKE_CXX_COMPILER_ID}")
  add_subdirectory(hash-test)
else()
  message(STATUS
    "Skipping pbds-based benchmarks (hash-test, pq-test/legacy-benchmark). "
    "Compiler is ${CMAKE_CXX_COMPILER_ID}.")
endif()

# The non-pbds tests/tools under pq-test are always built (the pbds part is
# controlled by the macro above and by PBDS_BENCHMARKS).
add_subdirectory(pq-test)
