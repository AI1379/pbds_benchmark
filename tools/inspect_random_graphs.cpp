// Inspect the exact shared generator, rather than reimplementing its RNG.
// This diagnostic does not time an algorithm and does not alter the benchmarks.
// Build from proj2: g++ -std=c++20 -O2 -I pq-test tools/inspect_random_graphs.cpp -o inspect-random
// Run: ./inspect-random > reports/data/random-reachability.csv
#include "random-graph.hpp"
#include <cstdint>
#include <iostream>
#include <vector>

// Reachability is independent of weights: traverse every arc from source 1.
void inspect(const char *suite, const char *family, uint32_t n, uint32_t m) {
  const auto graph = dijkstra_bench::MakeRandomGraph(n, m, 200000, 42);
  // A vertex is queued once, so traversal uses O(n) auxiliary space.
  std::vector<uint8_t> seen(n + 1);
  std::vector<uint32_t> queue{1};
  seen[1] = 1;
  uint64_t arcs = 0;
  // Count arcs scanned by a complete traversal of the reachable component.
  for (size_t head = 0; head < queue.size(); ++head) {
    for (const auto &edge : graph[queue[head]]) {
      ++arcs;
      if (!seen[edge.v]) {
        seen[edge.v] = 1;
        queue.push_back(edge.v);
      }
    }
  }
  std::cout << suite << ',' << family << ',' << n << ',' << m << ','
            << queue.size() << ',' << arcs << '\n';
}

int main() {
  std::cout << "suite,scenario,n,m,reachable_vertices,reachable_arcs\n";
  // Mirror the v2 registration sweep exactly, including its irregular steps.
  for (uint32_t n = 10; n < 200000; n = n <= 10000 ? n * 16 : n * 2) {
    for (uint32_t m = n - 1; m < uint64_t(n) * (n - 1) / 2 && m < 500000;
         m = m < 10000 ? m * 8 : m * 2) {
      inspect("dij-v2", "random", n, m);
    }
  }
  // Fixed-density families use the same seed and source as the timed suite.
  for (uint32_t n = 1 << 10; n <= (1 << 17); n *= 2)
    inspect("complexity", "sparse", n, 4 * n);
  for (uint32_t n = 1 << 7; n <= (1 << 12); n *= 2)
    inspect("complexity", "dense", n, n * (n - 1) / 4);
}
