//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once

#include "csr-graph.hpp"
#include "heap-adaptor.hpp"
#include <cstdint>
#include <limits>
#include <vector>

#ifndef GENERAL_DIJKSTRA_HPP
#define GENERAL_DIJKSTRA_HPP

namespace dijkstra_bench {

using Heap = StdHeapAdaptor;
inline void dijkstra(const Graph &g, uint32_t s, std::vector<int64_t> &dis,
                     Heap &heap) {
  constexpr int64_t INF = std::numeric_limits<int64_t>::max() / 4;
  // Use std::fill to make it more general despite a little performance drawback
  std::fill(dis.begin(), dis.end(), INF);

  heap.push_or_decrease(0, s);
  dis[s] = 0;
  while (!heap.empty()) {
    auto [d, u] = heap.pop();
    if (d > dis[u])
      continue;
    for (auto [v, w] : g[u]) {
      auto nd = dis[u] + w;
      if (dis[v] > nd) {
        dis[v] = nd;
        heap.push_or_decrease(dis[v], v);
      }
    }
  }
}

} // namespace dijkstra_bench

#endif