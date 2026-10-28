//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once
#ifndef HEAP_ADAPTOR_HPP
#define HEAP_ADAPTOR_HPP

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(DIJKSTRA_BENCH_HAS_PBDS)
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/priority_queue.hpp>
#endif

#define SELF static_cast<Derived *>(this)

namespace dijkstra_bench {

using HeapKey = int64_t;
using HeapVertex = uint32_t;
using HeapEntry = std::pair<HeapKey, HeapVertex>;

template <typename Derived, bool Decreaseable = false> struct BaseHeapAdaptor {
  using Key = HeapKey;
  using Vertex = HeapVertex;
  using Entry = HeapEntry;
  static constexpr bool decreaseable = Decreaseable;
  // Radix-heap style constraint: every push must use a key >= the last popped
  // key. General-purpose adaptors have no such restriction, so this is false
  // here and RadixHeap overrides it with true. Tests that push arbitrary keys
  // (the std::multiset model differential test) must skip such a heap.
  static constexpr bool requires_monotone_pushes = false;

  explicit BaseHeapAdaptor(Vertex n = 0) : epoch_(1) {
    if constexpr (Decreaseable) {
      stamp_.assign(static_cast<size_t>(n) + 1, 0);
    }
  }

  void clear() {
    if constexpr (Decreaseable) {
      if (++epoch_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        epoch_ = 1;
      }
    }
    SELF->clear_impl();
  }

  bool empty() { return SELF->empty_impl(); }

  Entry pop() {
    // Contract: the caller must check empty() first; calling pop() on an empty
    // heap is undefined behavior in Release builds.
    assert(!SELF->empty_impl());
    Entry e = SELF->pop_impl();
    if constexpr (Decreaseable) {
      // stamp_[u] == 0 means that node u is popped from the heap
      stamp_[e.second] = 0;
    }
    return e;
  }

  void push_or_decrease(Key dist, Vertex v) {
    if constexpr (Decreaseable) {
      // stamp_ has size n+1. An out-of-range v would first be read out of
      // bounds at stamp_[v] below, so validate it first.
      assert(static_cast<size_t>(v) < stamp_.size());
      // stamp_[v] != epoch_ means that the node has not been inserted in this
      // round yet.
      if (stamp_[v] != epoch_) {
        SELF->insert_impl(dist, v);
        stamp_[v] = epoch_;
      } else {
        SELF->decrease_impl(v, dist);
      }
    } else {
      SELF->push_impl(dist, v);
    }
  }

private:
  // Timestamp Optimization
  std::vector<uint32_t> stamp_;
  uint32_t epoch_;
};

class StdHeapAdaptor : public BaseHeapAdaptor<StdHeapAdaptor, false> {
  using Base = BaseHeapAdaptor<StdHeapAdaptor, false>;

public:
  using Key = Base::Key;
  using Vertex = Base::Vertex;
  using Entry = Base::Entry;

  explicit StdHeapAdaptor(Vertex n = 0) : Base(n) {}

private:
  friend struct BaseHeapAdaptor<StdHeapAdaptor, false>;

  using Queue =
      std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;

  void clear_impl() { queue_ = Queue(); }
  bool empty_impl() const { return queue_.empty(); }
  Entry pop_impl() {
    Entry e = queue_.top();
    queue_.pop();
    return e;
  }
  void push_impl(Key dist, Vertex v) { queue_.push({dist, v}); }

  Queue queue_;
};

#ifdef DIJKSTRA_BENCH_HAS_PBDS

template <typename Tag>
class PbdsHeapAdaptor : public BaseHeapAdaptor<PbdsHeapAdaptor<Tag>, true> {
  static_assert(!std::is_same_v<Tag, __gnu_pbds::binary_heap_tag>,
                "binary_heap_tag do not support decrease-key");
  using Base = BaseHeapAdaptor<PbdsHeapAdaptor<Tag>, true>;

public:
  using Key = typename Base::Key;
  using Vertex = typename Base::Vertex;
  using Entry = typename Base::Entry;
  using Queue = __gnu_pbds::priority_queue<Entry, std::greater<Entry>, Tag>;
  using Handle = typename Queue::point_iterator;

  explicit PbdsHeapAdaptor(Vertex n)
      : Base(n), handle_(static_cast<size_t>(n) + 1) {}

private:
  friend struct BaseHeapAdaptor<PbdsHeapAdaptor<Tag>, true>;

  void clear_impl() { queue_.clear(); }
  bool empty_impl() const { return queue_.empty(); }
  Entry pop_impl() {
    Entry e = queue_.top();
    queue_.pop();
    return e;
  }
  void insert_impl(Key dist, Vertex v) { handle_[v] = queue_.push({dist, v}); }
  void decrease_impl(Vertex v, Key dist) {
    queue_.modify(handle_[v], {dist, v});
  }

  Queue queue_;
  std::vector<Handle> handle_;
};

using PbdsPairingHeapAdaptor = PbdsHeapAdaptor<__gnu_pbds::pairing_heap_tag>;
using PbdsThinHeapAdaptor = PbdsHeapAdaptor<__gnu_pbds::thin_heap_tag>;
using PbdsBinomialHeapAdaptor = PbdsHeapAdaptor<__gnu_pbds::binomial_heap_tag>;
using PbdsRcBinomialHeapAdaptor =
    PbdsHeapAdaptor<__gnu_pbds::rc_binomial_heap_tag>;
#endif

} // namespace dijkstra_bench

#undef SELF

#endif
