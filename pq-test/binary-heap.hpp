//
// Created by Renatus Madrigal on 2026/10/27.
//

#pragma once

#include "heap-adaptor.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#ifndef BINARY_HEAP_HPP
#define BINARY_HEAP_HPP

namespace dijkstra_bench {

// Hand-written binary min-heap with decrease-key, adapted from the legacy
// benchmark in legacy-benchmark/dijkstra-heap.hpp. The legacy shape is kept on
// purpose:
//   * heap_[1..size_] stores vertices, ordered by key_[v] (1-based layout);
//   * idx_[v] is the position of vertex v in heap_, so decrease-key can jump
//     straight to the node instead of searching for it;
//   * key_[v] plays the role of the legacy global dis[] array, and cmp() is the
//     same predicate: true iff lhs has the larger key and is thus a worse root.
// The class only adds the BaseHeapAdaptor contract (insert / decrease / pop /
// clear / empty) on top of that, so the actual heap operations stay identical
// to the legacy code.
class BinaryHeapAdaptor : public BaseHeapAdaptor<BinaryHeapAdaptor, true> {
  using Base = BaseHeapAdaptor<BinaryHeapAdaptor, true>;

public:
  using Key = Base::Key;
  using Vertex = Base::Vertex;
  using Entry = Base::Entry;

  explicit BinaryHeapAdaptor(Vertex n)
      : Base(n), size_(0), heap_(n + 1), idx_(n + 1), key_(n + 1) {}

private:
  friend Base;

  // Legacy cmp(heap[x], heap[y]): true iff the key of lhs is larger, i.e. lhs
  // is the worse candidate for the root.
  bool cmp(Vertex lhs, Vertex rhs) const { return key_[lhs] > key_[rhs]; }

  void pushUp(uint32_t x) {
    while (x > 1) {
      if (cmp(heap_[x / 2], heap_[x])) {
        std::swap(idx_[heap_[x / 2]], idx_[heap_[x]]);
        std::swap(heap_[x / 2], heap_[x]);
        x /= 2;
      } else {
        break;
      }
    }
  }

  void pushDown(uint32_t x) {
    uint32_t s = x * 2;
    while (s <= size_) {
      // Take the smaller of the two children; the legacy code spells this as
      // `s <= top - 1 && cmp(heap[s], heap[s + 1])`, i.e. "left > right".
      if (s < size_ && cmp(heap_[s], heap_[s + 1]))
        ++s;
      if (cmp(heap_[x], heap_[s])) {
        std::swap(idx_[heap_[x]], idx_[heap_[s]]);
        std::swap(heap_[x], heap_[s]);
        x = s;
        s = x * 2;
      } else {
        break;
      }
    }
  }

  // ---- BaseHeapAdaptor contract -------------------------------------------

  void clear_impl() {
    // O(1): stale idx_ entries are overwritten by insert_impl(), and Base's
    // epoch makes every old stamp_ invalid.
    size_ = 0;
  }

  bool empty_impl() { return size_ == 0; }

  Entry pop_impl() {
    // Contract of BaseHeapAdaptor::pop(): the caller must check empty() first.
    assert(!empty_impl());
    // Legacy Dijkstra's extract-min: take the root, move the last vertex into
    // the root slot and sift it down.
    const Entry res{key_[heap_[1]], heap_[1]};
    std::swap(idx_[heap_[1]], idx_[heap_[size_]]);
    std::swap(heap_[1], heap_[size_]);
    --size_;
    pushDown(1);
    return res;
  }

  void insert_impl(Key dist, Vertex v) {
    // BaseHeapAdaptor's stamp_ guarantees that v is not in the heap right now,
    // so the slot of v is free.
    assert(v >= 1 && static_cast<size_t>(v) < heap_.size());
    key_[v] = dist;
    heap_[++size_] = v;
    idx_[v] = size_;
    pushUp(size_);
  }

  void decrease_impl(Vertex v, Key dist) {
    // BaseHeapAdaptor's stamp_ guarantees that v is already in the heap, so
    // idx_[v] is its current slot; Dijkstra only ever passes a smaller key.
    assert(v >= 1 && static_cast<size_t>(v) < heap_.size());
    assert(dist <= key_[v]);
    key_[v] = dist;
    pushUp(idx_[v]);
  }

  uint32_t size_;
  std::vector<Vertex> heap_;  // 1-based binary heap of vertex ids
  std::vector<uint32_t> idx_; // vertex id -> position in heap_
  std::vector<Key> key_;      // vertex id -> current key (legacy dis[])
};

} // namespace dijkstra_bench

#endif
