pbds_add_benchmark(dij-bench dij-bench.cpp)
pbds_add_benchmark(dij-bench-hack-spfa dij-bench-hack-spfa.cpp)

# 图生成器的功能测试（不是 benchmark，自己带 main）
add_executable(graph-gen-test graph-gen-test.cpp)
if(MSVC)
  target_compile_options(graph-gen-test PRIVATE /W4 /utf-8)
else()
  target_compile_options(graph-gen-test PRIVATE -Wall -Wextra)
endif()
