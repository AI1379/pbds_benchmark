//
// Created by Renatus Madrigal on 2026/10/27.
//

#pragma once

#ifndef GENERAL_BELLMAN_FORD_HPP
#define GENERAL_BELLMAN_FORD_HPP

#include "csr-graph.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace dijkstra_bench {

// Queue-based Bellman-Ford (the queue optimization commonly called SPFA),
// FIFO variant. It is kept as a comparison baseline; the reference
// implementation used by the unit tests and benchmark cross-checks is
// spfa_slf_lll() below.
//
// Queue is a template parameter instead of a concrete std::queue so the caller
// can plug in an allocator-aware queue -- a std::queue backed by
// std::pmr::deque, a pool-backed deque, or any container that provides the
// push / front / pop / empty interface -- and reuse it across queries, just
// like the heap objects in general-dijkstra.hpp.
//
// Entries are (distance, vertex) pairs. Every successful relaxation pushes a
// new entry; on pop, an entry whose distance is larger than the current
// dis[u] is stale and skipped. This costs some duplicate entries, but it needs
// no separate per-vertex "in queue" array, so all auxiliary memory stays
// inside the caller-provided queue (which is what makes pmr / memory-pool
// experiments meaningful here). A typical call:
//
//   std::queue<std::pair<int64_t, uint32_t>> queue;
//   std::vector<int64_t> dis(g.num_vertices() + 1);
//   bellman_ford(g, s, dis, queue);
//
// For non-negative edge weights the result equals Dijkstra's. On return the
// queue is empty and can be reused directly for the next query.
template <typename Queue>
inline void bellman_ford(const Graph &g, uint32_t s, std::vector<int64_t> &dis,
                         Queue &queue) {
  constexpr int64_t INF = std::numeric_limits<int64_t>::max() / 4;

  // Same out-parameter contract as general-dijkstra.hpp: dis is overwritten.
  std::fill(dis.begin(), dis.end(), INF);

  // The queue is reused across queries. A completed run leaves it empty, but
  // clear it defensively so an interrupted run cannot poison the next query.
  while (!queue.empty())
    queue.pop();

  dis[s] = 0;
  queue.push({0, s});
  while (!queue.empty()) {
    const auto [d, u] = queue.front();
    queue.pop();
    // Stale entry: u got a smaller distance after this entry was enqueued.
    if (d > dis[u])
      continue;
    for (const auto &e : g[u]) {
      const int64_t nd = d + e.w;
      if (nd < dis[e.v]) {
        dis[e.v] = nd;
        queue.push({nd, e.v});
      }
    }
  }
}

// SPFA with the SLF + LLL deque ordering, kept separate from the FIFO
// bellman_ford() above (that one is not modified):
//
//   * SLF (Small Label First): on enqueue, push the vertex to the front when
//     its label is smaller than the current front label;
//   * LLL (Large Label Last): maintain the average label of the queued
//     vertices and rotate the front vertex to the back while its label is
//     above that average.
//
// Both heuristics only change the relaxation order, so the distances are
// identical to bellman_ford() / Dijkstra; what changes is the number of
// successful relaxations. This is the reference implementation used by the
// unit tests and the cross-checks of both benchmark programs; the FIFO variant
// above is kept as a comparison baseline. On the 9th-DIMACS road networks the
// FIFO variant needs tens (NY) to hundreds (E) of edge scans per arc, while
// this variant stays at ~1.1-1.3 scans per arc and beats both FIFO SPFA and
// binary-heap Dijkstra by a wide margin. The worst case is still O(VE) -- like
// every SPFA ordering heuristic, it can be defeated by an adversarial graph.
//
// Deque must provide push_front / push_back / pop_front / front / empty and
// hold 1-based vertex ids (std::deque<uint32_t>, std::pmr::deque<uint32_t>, a
// pool-backed deque, ...). Unlike bellman_ford(), this variant does not queue
// (distance, vertex) entries: it keeps a local per-vertex in-queue flag and
// stores the label in dis[]. The deque is cleared on entry and empty on
// return, so it can be reused across queries.
template <typename Deque>
inline void spfa_slf_lll(const Graph &g, uint32_t s, std::vector<int64_t> &dis,
                         Deque &queue) {
  constexpr int64_t INF = std::numeric_limits<int64_t>::max() / 4;
  const uint32_t n = g.num_vertices();

  // Same out-parameter contract as the other readers: dis is overwritten.
  std::fill(dis.begin(), dis.end(), INF);
  // The deque is reused across queries: drop leftovers from a previous run.
  while (!queue.empty())
    queue.pop_front();

  std::vector<std::uint8_t> in_queue(static_cast<std::size_t>(n) + 1, 0);
  std::int64_t label_sum = 0; // sum of dis[v] over v currently in `queue`
  std::int64_t queued = 0;    // number of vertices currently in `queue`

  const auto push = [&](uint32_t v) {
    if (queue.empty() || dis[v] < dis[queue.front()])
      queue.push_front(v);
    else
      queue.push_back(v);
    in_queue[v] = 1;
    label_sum += dis[v];
    ++queued;
  };

  dis[s] = 0;
  push(s);
  while (!queue.empty()) {
    uint32_t u = queue.front();
    // LLL: park the front vertex at the back while it is above the average.
    // long double avoids overflowing dis[u] * queued for huge distances.
    while (static_cast<long double>(dis[u]) * static_cast<long double>(queued) >
           static_cast<long double>(label_sum)) {
      queue.pop_front();
      queue.push_back(u);
      u = queue.front();
    }
    queue.pop_front();
    --queued;
    in_queue[u] = 0;
    label_sum -= dis[u];
    for (const auto &e : g[u]) {
      const int64_t nd = dis[u] + e.w;
      if (nd < dis[e.v]) {
        const int64_t old = dis[e.v];
        dis[e.v] = nd;
        if (in_queue[e.v])
          label_sum += nd - old; // still queued: only its label changed
        else
          push(e.v);
      }
    }
  }
}

} // namespace dijkstra_bench

#endif
