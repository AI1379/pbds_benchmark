//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once

#include "heap-adaptor.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#ifndef FIBONACCI_HEAP_HPP
#define FIBONACCI_HEAP_HPP

namespace dijkstra_bench {

class FibonacciHeap : public BaseHeapAdaptor<FibonacciHeap, true> {
  using Base = BaseHeapAdaptor<FibonacciHeap, true>;

  struct FibNode {
    int64_t key;
    FibNode *parent;
    FibNode *child;

    FibNode *left;
    FibNode *right;

    uint32_t degree;
    bool mark;
  };

public:
  using Key = Base::Key;
  using Vertex = Base::Vertex;
  using Entry = Base::Entry;

  explicit FibonacciHeap(Vertex n)
      : Base(n), min_root_(nullptr), buffer_(n + 1) {}

private:
  friend Base;

  void clear_impl() { min_root_ = nullptr; }

  bool empty_impl() { return min_root_ == nullptr; }

  void consolidate() {
    std::array<FibNode *, 64> root_of_deg{};
    FibNode *next = min_root_;

    min_root_ = nullptr;

    while (next) {
      auto cur = next;

      if (cur->right == cur)
        next = nullptr;
      else
        next = cur->right;

      remove_node(cur);

      // cascading merge
      while (true) {
        auto d = cur->degree;

        assert(d < 64);

        if (!root_of_deg[d]) {
          root_of_deg[d] = cur;
          break;
        }

        auto other = root_of_deg[d];
        root_of_deg[d] = nullptr;

        if (cur->key > other->key)
          std::swap(cur, other);

        add_child(other, cur);
      }
    }
    for (auto &ptr : root_of_deg) {
      if (ptr) {
        push_root_list(ptr);
        ptr = nullptr;
      }
    }
  }

  void cut_node(FibNode *ptr) {
    auto pa = ptr->parent;
    if (ptr->right == ptr) {
      pa->child = nullptr;
    } else {
      if (ptr == pa->child) {
        pa->child = ptr->right;
      }
      remove_node(ptr);
    }
    --pa->degree;
    ptr->parent = nullptr;
    push_root_list(ptr);
  }

  void cascading_cut(FibNode *ptr) {
    while (ptr->parent) {
      auto pa = ptr->parent;
      cut_node(ptr);

      if (!pa->mark) {
        if (pa->parent) // pa is not root
          pa->mark = true;
        break;
      }

      ptr = pa;
    }
  }

  FibNode *initialize_node(FibNode *pa, Key key, Vertex idx) {
    auto res = &buffer_[idx];

    // Initialize as a loop
    res->left = res->right = res;
    res->key = key;
    res->parent = pa;

    res->child = nullptr;
    res->degree = 0;
    res->mark = false;

    return res;
  }

  void link_node(FibNode *ptr, FibNode *new_next) {
    auto next = ptr->right;
    ptr->right = new_next;
    new_next->left = ptr;

    next->left = new_next;
    new_next->right = next;
  }

  void remove_node(FibNode *ptr) {
    auto next = ptr->right, pre = ptr->left;

    pre->right = next;
    next->left = pre;

    ptr->left = ptr->right = ptr;
  }

  void add_child(FibNode *ptr, FibNode *pa) {
    ptr->parent = pa;
    ptr->mark = false;

    if (pa->child == nullptr) {
      pa->child = ptr;
    } else {
      link_node(pa->child, ptr);
    }

    ++pa->degree;
  }

  Vertex get_vertex(FibNode *ptr) {
    assert(buffer_.data() <= ptr && ptr < buffer_.data() + buffer_.size());
    return ptr - buffer_.data();
  }

  // CLRS 19.2 EXTRACT-MIN, with a single code path for every case:
  //   1) unlink z from the root list; the root list becomes empty iff z was
  //      the only root;
  //   2) promote every child of z to the root list, clearing its parent and
  //      mark explicitly (the "add x to the root list; x.p = NIL" step);
  //   3) consolidate() rebuilds the root list and recomputes the minimum.
  //
  // Promoting the children costs O(degree), which is the textbook cost of
  // EXTRACT-MIN; consolidate() visits these nodes again anyway.
  Entry pop_impl() {
    FibNode *z = min_root_;
    Entry res = {z->key, get_vertex(z)};

    // 1) Unlink z from the root list.
    if (z->right == z) {
      min_root_ = nullptr;
    } else {
      min_root_ = z->right;
      remove_node(z);
    }

    // 2) Promote the children of z to the root list.
    FibNode *c = z->child;
    if (c) {
      FibNode *cur = c;
      do {
        FibNode *nxt = cur->right;
        cur->parent = nullptr;
        cur->mark = false;
        // push_root_list() expects a single-node ring: detach cur from the
        // sibling ring first, otherwise a promoted child would keep using a
        // stale sibling pointer as its root-list neighbour.
        cur->left = cur->right = cur;
        push_root_list(cur);
        cur = nxt;
      } while (cur != c);
    }

    // 3) Detach the popped node; its slot will be reused by a later insert.
    z->child = nullptr;
    z->degree = 0;
    z->mark = false;
    z->left = z->right = z;

    if (min_root_)
      consolidate();

    return res;
  }

  void push_root_list(FibNode *ptr) {
    ptr->mark = false;
    if (!min_root_) {
      min_root_ = ptr;
      return;
    }
    link_node(min_root_, ptr);
    if (ptr->key < min_root_->key)
      min_root_ = ptr;
  }

  void insert_impl(Key dist, Vertex v) {
    auto ptr = initialize_node(nullptr, dist, v);
    push_root_list(ptr);
  }

  void decrease_impl(Vertex v, Key dist) {
    auto ptr = &buffer_[v];
    assert(dist <= ptr->key);
    ptr->key = dist;
    if (ptr->parent && ptr->key < ptr->parent->key) {
      // Cascading cut
      cascading_cut(ptr);
    } else if (!ptr->parent && ptr->key < min_root_->key) {
      // New minimum root
      min_root_ = ptr;
    }
  }

  FibNode *min_root_;
  std::vector<FibNode> buffer_;
};

} // namespace dijkstra_bench

#endif