//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once

#include "./heap-adaptor.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <vector>

#ifndef SEGMENT_TREE_HEAP_HPP
#define SEGMENT_TREE_HEAP_HPP

namespace dijkstra_bench {

class SegmentTreeHeap : public BaseHeapAdaptor<SegmentTreeHeap, true> {
  using Base = BaseHeapAdaptor<SegmentTreeHeap, true>;
  static constexpr int64_t INF = std::numeric_limits<int64_t>::max() / 4;

public:
  using Key = Base::Key;
  using Vertex = Base::Vertex;
  using Entry = Base::Entry;

  explicit SegmentTreeHeap(Vertex n = 0)
      : Base(n), size_(n), tree_(n * 2 + 2, INF), min_vertex_(n * 2 + 2, 0) {
    for (auto i = size_ + 1; i <= size_ * 2; ++i) {
      min_vertex_[i] = i - size_;
    }
    for (auto i = size_; i; --i) {
      // Currently all of the nodes are INF, thus both i*2 and i*2+1 are the
      // minimum one. Just choose one of them.
      min_vertex_[i] = min_vertex_[i * 2];
    }
  }

private:
  friend Base;

  void clear_impl() { std::fill(tree_.begin(), tree_.end(), INF); }

  bool empty_impl() { return tree_[1] == INF; }

  void modify(Key dist, Vertex v) {
    // Vertex indices are 1-based: v == 0 would write to internal node n, and
    // v == n+1 would corrupt the sentinel leaf 2n+1 (the safety of the whole
    // 2n+2 layout depends on that leaf always being INF); any larger v writes
    // out of bounds.
    assert(v >= 1 && v <= size_);
    auto cur = size_ + v;
    tree_[cur] = dist;
    cur >>= 1;
    while (cur) {
      auto new_min = std::min(tree_[cur * 2], tree_[cur * 2 + 1]);
      auto new_min_v_ = (new_min == tree_[cur * 2]) ? min_vertex_[cur * 2]
                                                    : min_vertex_[cur * 2 + 1];

      if (new_min == tree_[cur] && new_min_v_ == min_vertex_[cur])
        break;

      tree_[cur] = new_min;
      min_vertex_[cur] = new_min_v_;

      cur >>= 1;
    }
  }

  Entry pop_impl() {
    // Contract of BaseHeapAdaptor::pop(): the caller must check empty() first.
    assert(!empty_impl());
    auto res = Entry{tree_[1], min_vertex_[1]};
    modify(INF, min_vertex_[1]);
    return res;
  }

  void insert_impl(Key dist, Vertex v) {
    // BaseHeapAdaptor's stamp_ guarantees that v is not in the heap right now,
    // so its leaf must be empty.
    assert(v >= 1 && v <= size_ && tree_[size_ + v] == INF);
    modify(dist, v);
  }

  void decrease_impl(Vertex v, Key dist) {
    // BaseHeapAdaptor's stamp_ guarantees that v was already inserted in this
    // round; Dijkstra only passes strictly smaller distances for v.
    assert(v >= 1 && v <= size_ && tree_[size_ + v] != INF &&
           dist < tree_[size_ + v]);
    modify(dist, v);
  }

  uint32_t size_;

  // Use separated vector for better cache rate
  std::vector<int64_t> tree_;
  std::vector<uint32_t> min_vertex_;
};

} // namespace dijkstra_bench

#endif
