//
// Random directed graph generator, shared by the unit tests and by the three
// benchmark programs (dij-bench-v2 / dij-bench-dimacs / dij-bench-complexity).
//
// The generator is deliberately deterministic: benchmark inputs must be
// reproducible run to run and machine to machine, otherwise the implementation
// comparison would be meaningless. It is also allocation-light: the arcs are
// generated into per-vertex buckets and then flattened into the project's CSR
// layout (csr-graph.hpp) in one pass.
//

#pragma once
#ifndef RANDOM_GRAPH_HPP
#define RANDOM_GRAPH_HPP

#include "csr-graph.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace dijkstra_bench {

/// Builds a random directed graph on `n` vertices with exactly `m` arcs.
///
/// Properties (all relevant to the benchmark programs):
///   * vertex ids are 1-based (`1 .. n`), like every other graph in the
///     project, so the heaps and the DIMACS parser agree on indexing;
///   * arc weights are uniform in `[0, wmax]` (both ends inclusive). A small
///     `wmax` (e.g. 2) produces many tied shortest distances, which is what
///     the unit tests want; a large one (e.g. 200000) is the benchmark setup.
///     Weight 0 is allowed on purpose: it makes Dijkstra observe equal
///     distances without negative edges;
///   * self-loops are skipped when `n > 1` (a self-loop can never improve a
///     shortest distance, it would only waste relaxations); a single-vertex
///     graph has no other arc to offer, so `n == 1` falls back to self-loops;
///   * parallel arcs are allowed, so no de-duplication pass is needed and any
///     `m` is generatable in O(m) expected time (the graph is a multi-graph);
///   * the result depends only on `(n, m, wmax, seed)`, so the same arguments
///     always produce the same graph.
///
/// The returned Graph stores the arcs grouped by tail vertex in ascending id
/// order (the CSR requirement); the adjacency order inside one list is the
/// generation order.
inline Graph MakeRandomGraph(uint32_t n, uint32_t m, int64_t wmax,
                             uint32_t seed) {
  assert(wmax >= 0);
  if (n == 0) {
    // No vertices: only the empty graph makes sense.
    assert(m == 0);
    return Graph();
  }

  std::mt19937 rng(seed);
  std::uniform_int_distribution<uint32_t> vertex_dist(1, n);
  std::uniform_int_distribution<int64_t> weight_dist(0, wmax);

  // Bucket the arcs by tail first; flattening to CSR is then one linear pass.
  using ArcList = std::vector<Graph::Edge>;
  std::vector<ArcList> adjacency(static_cast<std::size_t>(n) + 1);
  for (uint32_t i = 0; i < m; ++i) {
    const uint32_t tail = vertex_dist(rng);
    uint32_t head = vertex_dist(rng);
    if (n > 1) {
      while (head == tail)
        head = vertex_dist(rng);
    }
    adjacency[tail].push_back({head, weight_dist(rng)});
  }

  // Flatten: offset[u] is one past the last arc of vertex u (offset[0] == 0),
  // which is the convention Graph's CSR constructor validates.
  std::vector<uint32_t> offset(static_cast<std::size_t>(n) + 1, 0);
  std::vector<Graph::Edge> edges;
  edges.reserve(m);
  for (uint32_t u = 1; u <= n; ++u) {
    for (const Graph::Edge &edge : adjacency[u])
      edges.push_back(edge);
    offset[u] = static_cast<uint32_t>(edges.size());
  }
  return Graph(std::move(offset), std::move(edges));
}

} // namespace dijkstra_bench

#endif
