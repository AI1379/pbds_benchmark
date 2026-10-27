//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once
#ifndef CSR_GRAPH_HPP
#define CSR_GRAPH_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dijkstra_bench {

/// Directed graph stored in compressed sparse row (CSR) form.
///
/// Vertex identifiers are 1-based: valid ids are `1 .. num_vertices()`.  This
/// matches the DIMACS shortest-path file format and the 1-based arrays used by
/// the existing benchmarks, so a parsed graph can be fed to them without an
/// index translation step.
///
/// Internally the adjacency array is the standard 0-based CSR layout: `offset_`
/// has `n + 1` entries, where the arcs of vertex `u` live in
/// `[offset_[u - 1], offset_[u])`.  `operator[](u)` hides that translation.
///
/// The arc order inside one adjacency list is the order in which the arcs were
/// inserted (for DIMACS files: the order in the file).
class Graph {
public:
  struct Edge {
    uint32_t v; ///< head vertex, 1-based
    int64_t w;  ///< edge weight
  };

  Graph() = default;

  /// Build a graph directly from CSR arrays.
  ///
  /// Requirements: `offset` is non-empty, `offset.front() == 0` and
  /// `offset.back() == edges.size()`, and every `Edge::v` is in
  /// `[1, offset.size() - 1]`.
  Graph(std::vector<uint32_t> offset, std::vector<Edge> edges)
      : offset_(std::move(offset)), edges_(std::move(edges)) {
    if (offset_.empty() || offset_.front() != 0 ||
        offset_.back() != edges_.size()) {
      throw std::invalid_argument("Graph: inconsistent CSR arrays");
    }
  }

  /// Outgoing arcs of vertex `u` (1-based, `1 <= u <= num_vertices()`).
  std::span<const Edge> operator[](uint32_t u) const {
    const std::size_t slot = static_cast<std::size_t>(u) - 1;
    return {edges_.data() + offset_[slot], offset_[slot + 1] - offset_[slot]};
  }

  /// Number of vertices `n` (the graph may be empty, then this is 0).
  uint32_t num_vertices() const noexcept {
    return offset_.empty() ? 0 : static_cast<uint32_t>(offset_.size() - 1);
  }

  /// Number of arcs `m`.
  std::size_t num_edges() const noexcept { return edges_.size(); }

  /// Raw CSR offsets: `offset_[u]` is one past the last arc of vertex `u`.
  /// `offsets().size() == num_vertices() + 1`.
  const std::vector<uint32_t> &offsets() const noexcept { return offset_; }

  /// Raw arc array; the arcs of vertex `u` occupy `[offset_[u-1], offset_[u])`.
  const std::vector<Edge> &edges() const noexcept { return edges_; }

private:
  std::vector<uint32_t> offset_;
  std::vector<Edge> edges_;
};

} // namespace dijkstra_bench

#endif
