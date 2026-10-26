cmake_minimum_required(VERSION 3.16)

project(pbds_benchmark LANGUAGES CXX)

# 本仓库源码使用的标准（与 .vscode/tasks.json 的 -std=c++20 一致）
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# vendored: GoogleTest (googletest + googlemock), 见 third_party/README.md
# ---------------------------------------------------------------------------
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# vendored: Google Benchmark, 见 third_party/README.md
# ---------------------------------------------------------------------------
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/benchmark EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# 公共辅助函数
# ---------------------------------------------------------------------------
# 添加一个使用 benchmark::benchmark_main（源码里已有 BENCHMARK_MAIN()）的可执行目标
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
# 编译器检测：hash-test / pq-test 依赖 GCC 的 <ext/pb_ds>，非 GCC 编译器自动跳过
# ---------------------------------------------------------------------------
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(_pbds_benchmarks_default ON)
else()
  set(_pbds_benchmarks_default OFF)
endif()

option(PBDS_BENCHMARKS
  "Build benchmarks that depend on GCC's <ext/pb_ds> (hash-test, pq-test)"
  ${_pbds_benchmarks_default})

if(PBDS_BENCHMARKS)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(WARNING
      "PBDS_BENCHMARKS=ON but the compiler is ${CMAKE_CXX_COMPILER_ID} (not GCC); "
      "<ext/pb_ds> benchmarks are unlikely to compile")
  endif()
  message(STATUS "Building pbds-based benchmarks (hash-test, pq-test) with ${CMAKE_CXX_COMPILER_ID}")
  add_subdirectory(hash-test)
  add_subdirectory(pq-test)
else()
  message(STATUS
    "Compiler is ${CMAKE_CXX_COMPILER_ID} (not GCC): skipping pbds-based benchmarks "
    "(hash-test, pq-test). Pass -DPBDS_BENCHMARKS=ON to force.")
endif()
