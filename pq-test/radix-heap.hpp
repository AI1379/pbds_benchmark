//
// Created by Renatus Madrigal on 2026/10/28.
//

#pragma once

#include "heap-adaptor.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <vector>
#ifndef RADIX_HEAP_HPP
#define RADIX_HEAP_HPP

namespace dijkstra_bench {

class RadixHeap : public BaseHeapAdaptor<RadixHeap, true> {
  using Base = BaseHeapAdaptor<RadixHeap, true>;

public:
  using Key = Base::Key;
  using Vertex = Base::Vertex;
  using Entry = Base::Entry;

  // A radix heap is only correct if keys are inserted in nondecreasing order:
  // every push must use a key >= the last popped key (see the assert in
  // insert_impl). Dijkstra preserves this because edge weights are
  // non-negative; arbitrary push sequences do not.
  static constexpr bool requires_monotone_pushes = true;

  explicit RadixHeap(Vertex n)
      : Base(n), last_(0), bucket_state_(0), handle_(n + 1) {}

private:
  friend Base;

  static constexpr Key INF = std::numeric_limits<Key>::max() / 4;
  static constexpr uint32_t INVALID_HANDLE = 0xffffffff;

  void clear_impl() {
    for (auto &b : buckets_)
      b.clear();
    bucket_state_ = 0;
    // handle_.clear();
    last_ = 0;
  }

  bool empty_impl() const { return bucket_state_ == 0; }

  static uint32_t get_bucket(Key k, Key l) {
    return std::bit_width(static_cast<uint64_t>(k ^ l));
  }

  Entry pop_impl() {
    if (buckets_[0].empty()) {
      auto idx = std::countr_zero(bucket_state_);
      auto min_ = buckets_[idx][0].first;
      for (auto [v, _] : buckets_[idx]) {
        min_ = std::min(v, min_);
      }

      last_ = min_;

      for (auto &[key, v] : buckets_[idx]) {
        insert_impl(key, v);
      }

      buckets_[idx].clear();
      // currently the idx bit of bucket_state_ must be 1,
      // so just xor 1ull << idx
      bucket_state_ ^= 1ull << idx;
    }

    auto res = buckets_[0].back();
    buckets_[0].pop_back();
    if (buckets_[0].empty()) {
      bucket_state_ ^= 1;
      // Drained: reset the monotone lower bound so the heap is immediately
      // reusable for the next query without clear(). The benchmark loops rely
      // on this "drained => reusable" contract (see general-dijkstra.hpp);
      // without the reset the next push of distance 0 would violate
      // dist >= last_ and the assert in insert_impl would fire.
      if (bucket_state_ == 0)
        last_ = 0;
    }

    handle_[res.second] = INVALID_HANDLE;
    return res;
  }

  void insert_impl(Key dist, Vertex v) {
    assert(dist >= last_);

    auto idx = get_bucket(dist, last_);
    // 32 buckets cover keys below 2^31, well above the road-network and
    // random-graph distances used here; check the assumption instead of
    // indexing out of bounds. Widen buckets_/bucket_state_ to 64 buckets if
    // larger keys are ever needed.
    assert(idx < buckets_.size());
    buckets_[idx].emplace_back(dist, v);
    bucket_state_ |= 1ull << idx;
    // 7 bits bucket id | 25 bits position in bucket
    handle_[v] = (idx << 25) | (buckets_[idx].size() - 1);
  }

  void decrease_impl(Vertex v, Key dist) {
    auto bk = handle_[v] >> 25;
    auto idx = handle_[v] & ((1u << 25) - 1);

    auto nb = get_bucket(dist, last_);

    if (nb == bk) {
      buckets_[bk][idx].first = dist;
      return;
    }

    // nb != bk, we need to remove it and push to another bucket
    // First get the original tail
    auto [tk, tv] = buckets_[bk].back();
    // Update the handle_ of original tail
    handle_[tv] = (bk << 25) | idx;

    // swap and pop
    std::swap(buckets_[bk][idx], buckets_[bk].back());
    buckets_[bk].pop_back();
    if (buckets_[bk].empty())
      bucket_state_ ^= 1u << bk;

    insert_impl(dist, v);
  }

  std::array<std::vector<Entry>, 32> buckets_;
  Key last_;
  uint32_t bucket_state_;
  std::vector<uint32_t> handle_;
};

} // namespace dijkstra_bench

#endif