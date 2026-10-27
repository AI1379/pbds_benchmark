//
// Unit tests for the heap adaptors in heap-adaptor.hpp (GoogleTest).
//
// Coverage:
//   * Common contract of every heap adaptor: empty / push_or_decrease / pop /
//     clear
//   * Randomized differential test against std::multiset (tied keys, lazy
//     deletion semantics, clear-then-reuse)
//   * End-to-end Dijkstra: differential test against Bellman-Ford on the same
//     random graph
//   * SegmentTreeHeap regressions: 1-based vertices, the sentinel leaf for
//     v == n, the stale min_vertex_ bug with tied keys, decrease semantics
//   * Contract assertions (Debug builds only): invalid vertex, pop on an empty
//     heap, non-strict decrease
//
// Note: the death tests rely on assert and are only enabled in builds that do
// not define NDEBUG. CMake's default Release build defines NDEBUG, so the
// default run executes the functional tests; the death tests run only in the
// `Debug` build.
//

#include "csr-graph.hpp"
#include "fibonacci-heap.hpp"
#include "heap-adaptor.hpp"
#include "segment-tree-heap.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <utility>
#include <vector>

namespace {

namespace db = dijkstra_bench;

using Key = db::HeapKey;
using Vertex = db::HeapVertex;
using Entry = db::HeapEntry;

constexpr Key kInf = std::numeric_limits<Key>::max() / 4;

// ---------------------------------------------------------------------------
// Reference implementations
// ---------------------------------------------------------------------------

/// Builds a random directed graph on n vertices (1-based). Weights are in
/// 0..2, which makes tied shortest distances likely.
db::Graph MakeRandomGraph(std::mt19937 &rng, Vertex n) {
  std::vector<std::vector<db::Graph::Edge>> adj(n + 1);
  const unsigned m = n + static_cast<unsigned>(rng() % (2 * n + 1));
  for (unsigned i = 0; i < m; ++i) {
    const Vertex u = static_cast<Vertex>(1 + rng() % n);
    const Vertex v = static_cast<Vertex>(1 + rng() % n);
    const Key w = static_cast<Key>(rng() % 3);
    adj[u].push_back({v, w});
  }
  std::vector<uint32_t> offset(n + 1, 0);
  std::vector<db::Graph::Edge> edges;
  for (Vertex u = 1; u <= n; ++u) {
    for (const auto &e : adj[u])
      edges.push_back(e);
    offset[u] = static_cast<uint32_t>(edges.size());
  }
  return db::Graph(std::move(offset), std::move(edges));
}

/// Bellman-Ford (non-negative weights, no negative cycles) as an independent
/// reference that does not use any heap.
std::vector<Key> BellmanFord(const db::Graph &graph, Vertex source) {
  const Vertex n = graph.num_vertices();
  std::vector<Key> dis(static_cast<size_t>(n) + 1, kInf);
  dis[source] = 0;
  for (Vertex iter = 1; iter < n; ++iter) {
    bool changed = false;
    for (Vertex u = 1; u <= n; ++u) {
      if (dis[u] == kInf)
        continue;
      for (const db::Graph::Edge &e : graph[u]) {
        const Key nd = dis[u] + e.w;
        if (nd < dis[e.v]) {
          dis[e.v] = nd;
          changed = true;
        }
      }
    }
    if (!changed)
      break;
  }
  return dis;
}

/// Generic Dijkstra kernel (same skeleton as section 6.4 of
/// docs/fibonacci-heap.md).
template <typename Heap>
std::vector<Key> RunDijkstra(const db::Graph &graph, Vertex source,
                             Heap &heap) {
  const Vertex n = graph.num_vertices();
  std::vector<Key> dis(static_cast<size_t>(n) + 1, kInf);
  heap.clear();
  dis[source] = 0;
  heap.push_or_decrease(0, source);
  while (!heap.empty()) {
    const auto [d, u] = heap.pop();
    if (d > dis[u])
      continue; // the lazy-deletion heap (StdHeapAdaptor) pops stale entries
    for (const db::Graph::Edge &e : graph[u]) {
      const Key nd = d + e.w;
      if (nd < dis[e.v]) {
        dis[e.v] = nd;
        heap.push_or_decrease(nd, e.v);
      }
    }
  }
  return dis;
}

// ---------------------------------------------------------------------------
// Model differential test: std::multiset as the reference model
//
// Both semantics are covered:
//   * decreaseable heaps (SegmentTreeHeap / pb_ds): each vertex has at most one
//     entry in the heap, so a second push_or_decrease is equivalent to a
//     decrease (the key must be strictly smaller).
//   * lazy-deletion heap (StdHeapAdaptor): push_or_decrease just pushes another
//     entry; the old entry stays in the heap, so a popped entry whose key is
//     greater than the current key is stale.
// Every pop must return a key equal to the reference minimum and the pair
// (key, vertex) must exist in the model. A decreaseable heap must additionally
// never pop the same vertex twice and never pop a stale key.
// ---------------------------------------------------------------------------
template <typename Heap>
void CheckModelDifferential(Vertex n, int trials, int steps, unsigned key_mod,
                            unsigned seed) {
  std::mt19937 rng(seed);
  for (int trial = 0; trial < trials; ++trial) {
    Heap heap(n);
    for (int round = 0; round < 2; ++round) {
      if (round == 1) {
        heap.clear(); // clear an empty heap, then keep reusing the same object
      }
      std::multiset<Entry> all;          // every entry currently in the heap
      std::vector<Key> cur(n + 1, kInf); // key most recently pushed per vertex
      std::vector<bool> finalized(n + 1, false);

      // Simulates one Dijkstra relaxation: give a not-yet-finalized vertex a
      // smaller key.
      auto try_relax = [&]() -> bool {
        for (int attempt = 0; attempt < 64; ++attempt) {
          const Vertex v = static_cast<Vertex>(1 + rng() % n);
          if (finalized[v] || cur[v] == 0)
            continue; // already finalized, or key is 0 and cannot shrink
          const Key k =
              (cur[v] == kInf)
                  ? static_cast<Key>(rng() % key_mod)
                  : static_cast<Key>(rng() % static_cast<unsigned>(cur[v]));
          if constexpr (Heap::decreaseable) {
            if (cur[v] != kInf)
              all.erase(std::make_pair(cur[v], v));
          }
          all.insert(std::make_pair(k, v));
          cur[v] = k;
          heap.push_or_decrease(k, v);
          return true;
        }
        return false;
      };

      auto check_pop = [&]() -> bool {
        if (all.empty() || heap.empty()) {
          ADD_FAILURE() << "heap/model out of sync: model.size=" << all.size()
                        << " heap.empty=" << heap.empty();
          return false;
        }
        const Key expected = all.begin()->first;
        const auto [k, v] = heap.pop();
        if (k != expected) {
          ADD_FAILURE() << "popped key=" << k
                        << ", reference minimum=" << expected;
          return false;
        }
        const auto it = all.find(std::make_pair(k, v));
        if (it == all.end()) {
          ADD_FAILURE() << "popped an entry missing from the reference model ("
                        << k << "," << v << ")";
          return false;
        }
        all.erase(it);
        if constexpr (Heap::decreaseable) {
          if (finalized[v]) {
            ADD_FAILURE() << "decreaseable heap popped vertex " << v
                          << " twice";
            return false;
          }
          if (k != cur[v]) {
            ADD_FAILURE() << "decreaseable heap popped a stale key: " << k
                          << " != " << cur[v];
            return false;
          }
          finalized[v] = true;
        } else {
          // Lazy-deletion heap: only an entry whose key equals the current
          // latest key counts as a valid finalization.
          if (!finalized[v] && k == cur[v])
            finalized[v] = true;
        }
        return true;
      };

      for (int step = 0; step < steps; ++step) {
        const bool want_push = all.empty() || (rng() % 100) < 55;
        bool pushed = false;
        if (want_push)
          pushed = try_relax();
        if (!pushed) {
          if (all.empty()) {
            if (!try_relax())
              break; // every vertex is finalized and the heap is empty
          } else if (!check_pop()) {
            return; // the model already diverged; continuing is pointless
          }
        }
        if (heap.empty() != all.empty()) {
          ADD_FAILURE() << "heap.empty()=" << heap.empty()
                        << " does not match the model (model.size="
                        << all.size() << ")";
          return;
        }
      }

      while (!all.empty()) {
        if (!check_pop())
          return;
      }
      if (!heap.empty()) {
        ADD_FAILURE() << "the model is empty but the heap is not";
        return;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Common tests for all heap adaptors (typed tests)
// ---------------------------------------------------------------------------

template <typename T> class HeapAdaptorTest : public ::testing::Test {};

using HeapTypes = ::testing::Types<
#ifdef DIJKSTRA_BENCH_HAS_PBDS
    db::PbdsPairingHeapAdaptor, db::PbdsThinHeapAdaptor,
    db::PbdsBinomialHeapAdaptor, db::PbdsRcBinomialHeapAdaptor,
#endif
    db::SegmentTreeHeap, db::StdHeapAdaptor, db::FibonacciHeap>;

TYPED_TEST_SUITE(HeapAdaptorTest, HeapTypes);

TYPED_TEST(HeapAdaptorTest, EmptyOnConstruction) {
  TypeParam heap(8);
  EXPECT_TRUE(heap.empty());
}

TYPED_TEST(HeapAdaptorTest, ZeroVerticesHeapIsEmpty) {
  TypeParam heap(0);
  EXPECT_TRUE(heap.empty());
}

TYPED_TEST(HeapAdaptorTest, PushThenPopSingle) {
  TypeParam heap(8);
  heap.push_or_decrease(42, 3);
  EXPECT_FALSE(heap.empty());
  const auto [k, v] = heap.pop();
  EXPECT_EQ(k, 42);
  EXPECT_EQ(v, 3u);
  EXPECT_TRUE(heap.empty());
}

TYPED_TEST(HeapAdaptorTest, PopsInNondecreasingOrder) {
  constexpr Vertex n = 16;
  TypeParam heap(n);
  for (Vertex v = 1; v <= n; ++v)
    heap.push_or_decrease(static_cast<Key>(10 + (v * 7) % 5), v); // tied keys
  Key prev = std::numeric_limits<Key>::min();
  std::vector<int> seen(n + 1, 0);
  int count = 0;
  while (!heap.empty()) {
    const auto [k, v] = heap.pop();
    EXPECT_LE(prev, k);
    ASSERT_GE(v, 1u);
    ASSERT_LE(v, n);
    ++seen[v];
    ++count;
    prev = k;
  }
  EXPECT_EQ(count, static_cast<int>(n));
  for (Vertex v = 1; v <= n; ++v)
    EXPECT_EQ(seen[v], 1) << "vertex " << v;
}

TYPED_TEST(HeapAdaptorTest, EqualKeysAreAllPoppedOnce) {
  constexpr Vertex n = 12;
  TypeParam heap(n);
  for (Vertex v = 1; v <= n; ++v)
    heap.push_or_decrease(7, v); // all keys are equal
  std::vector<bool> seen(n + 1, false);
  int count = 0;
  while (!heap.empty()) {
    const auto [k, v] = heap.pop();
    EXPECT_EQ(k, 7);
    ASSERT_GE(v, 1u);
    ASSERT_LE(v, n);
    EXPECT_FALSE(seen[v]) << "vertex " << v << " popped twice";
    seen[v] = true;
    ++count;
  }
  EXPECT_EQ(count, static_cast<int>(n));
}

TYPED_TEST(HeapAdaptorTest, ClearMakesEmptyAndReusable) {
  TypeParam heap(16);
  for (Vertex v = 1; v <= 4; ++v)
    heap.push_or_decrease(static_cast<Key>(v), v);
  heap.clear();
  EXPECT_TRUE(heap.empty());

  heap.push_or_decrease(9, 7);
  heap.push_or_decrease(4, 8);
  ASSERT_FALSE(heap.empty());
  const auto [k1, v1] = heap.pop();
  EXPECT_EQ(k1, 4);
  EXPECT_EQ(v1, 8u);
  const auto [k2, v2] = heap.pop();
  EXPECT_EQ(k2, 9);
  EXPECT_EQ(v2, 7u);
  EXPECT_TRUE(heap.empty());
}

TYPED_TEST(HeapAdaptorTest, DecreaseUpdatesKeyWithoutDuplicates) {
  if constexpr (TypeParam::decreaseable) {
    TypeParam heap(8);
    heap.push_or_decrease(10, 1);
    heap.push_or_decrease(20, 2);
    heap.push_or_decrease(3, 1); // decrease vertex 1
    const auto [k1, v1] = heap.pop();
    EXPECT_EQ(k1, 3);
    EXPECT_EQ(v1, 1u);
    const auto [k2, v2] = heap.pop();
    EXPECT_EQ(k2, 20);
    EXPECT_EQ(v2, 2u);
    EXPECT_TRUE(heap.empty());
  } else {
    // The lazy-deletion heap does not guarantee the absence of duplicate
    // entries; only check the order of valid pops here.
    TypeParam heap(8);
    heap.push_or_decrease(10, 1);
    heap.push_or_decrease(20, 2);
    heap.push_or_decrease(3, 1);
    std::vector<Key> keys;
    while (!heap.empty())
      keys.push_back(heap.pop().first);
    ASSERT_GE(keys.size(), 2u);
    EXPECT_EQ(keys[0], 3);
    // stale old entry of vertex 1; it still comes before 20
    EXPECT_EQ(keys[1], 10);
    EXPECT_EQ(keys.back(), 20);
  }
}

TYPED_TEST(HeapAdaptorTest, ModelDifferentialSmall) {
  CheckModelDifferential<TypeParam>(32, 8, 3000, 64, 12345u);
}

TYPED_TEST(HeapAdaptorTest, ModelDifferentialLarge) {
  CheckModelDifferential<TypeParam>(1000, 1, 50000, 4096, 987654u);
}

TYPED_TEST(HeapAdaptorTest, DijkstraMatchesBellmanFord) {
  std::mt19937 rng(20261026u);
  const Vertex sizes[] = {1, 2, 3, 5, 10, 30};
  for (Vertex n : sizes) {
    TypeParam heap(n);
    for (int trial = 0; trial < 4; ++trial) {
      const db::Graph graph = MakeRandomGraph(rng, n);
      for (int query = 0; query < 2; ++query) {
        const Vertex source = static_cast<Vertex>(1 + rng() % n);
        const std::vector<Key> want = BellmanFord(graph, source);
        const std::vector<Key> got = RunDijkstra(graph, source, heap);
        ASSERT_EQ(got.size(), want.size());
        for (Vertex v = 1; v <= n; ++v)
          ASSERT_EQ(got[v], want[v]) << "n=" << n << " trial=" << trial
                                     << " source=" << source << " vertex=" << v;
      }
    }
  }
}

// ---------------------------------------------------------------------------
// SegmentTreeHeap-specific tests
// ---------------------------------------------------------------------------

// The leaf of vertex n is 2n, and the second child of parent n is the sentinel
// leaf 2n+1. This used to allocate one element too few and read out of bounds;
// this test covers n = 1..33.
TEST(SegmentTreeHeapTest, VertexNIsUsable) {
  for (Vertex n = 1; n <= 33; ++n) {
    db::SegmentTreeHeap heap(n);
    heap.push_or_decrease(static_cast<Key>(100 + n), n);
    ASSERT_FALSE(heap.empty()) << "n=" << n;
    const auto [k, v] = heap.pop();
    EXPECT_EQ(k, 100 + n) << "n=" << n;
    EXPECT_EQ(v, n) << "n=" << n;
    EXPECT_TRUE(heap.empty()) << "n=" << n;
  }
}

// Regression: two vertices with equal keys share the same parent. In the old
// implementation min_vertex_ still pointed at the first popped vertex, so
// empty() never became true (infinite loop).
TEST(SegmentTreeHeapTest, EqualKeysRegression) {
  db::SegmentTreeHeap heap(5);
  heap.push_or_decrease(5, 1);
  heap.push_or_decrease(5, 2);

  std::multiset<Entry> got;
  while (!heap.empty())
    got.insert(heap.pop());
  const std::multiset<Entry> want{{5, 1}, {5, 2}};
  EXPECT_EQ(got, want);
}

// Regression: a tie occurs at the grandparent level (the other child of the
// root is also 7). The parent value changed but the root value did not, so the
// early break left min_vertex_ stale.
TEST(SegmentTreeHeapTest, AncestorTieRegression) {
  // the two children of the root cover {v3,v4} and {v1,v2}
  db::SegmentTreeHeap heap(5);
  heap.push_or_decrease(7, 3);
  heap.push_or_decrease(7, 1);

  std::multiset<Entry> got;
  while (!heap.empty())
    got.insert(heap.pop());
  const std::multiset<Entry> want{{7, 3}, {7, 1}};
  EXPECT_EQ(got, want);
}

TEST(SegmentTreeHeapTest, DecreaseKeepsSingleEntry) {
  db::SegmentTreeHeap heap(8);
  heap.push_or_decrease(10, 1);
  heap.push_or_decrease(20, 2);
  heap.push_or_decrease(3, 1);
  const auto [k1, v1] = heap.pop();
  EXPECT_EQ(k1, 3);
  EXPECT_EQ(v1, 1u);
  const auto [k2, v2] = heap.pop();
  EXPECT_EQ(k2, 20);
  EXPECT_EQ(v2, 2u);
  EXPECT_TRUE(heap.empty());
}

// Scale test: includes vertex n and random keys (some tied); after popping
// everything, every vertex appears exactly once.
TEST(SegmentTreeHeapTest, LargeInsertPopAll) {
  constexpr Vertex n = 200000;
  std::mt19937 rng(7u);
  db::SegmentTreeHeap heap(n);
  for (Vertex v = 1; v <= n; ++v)
    heap.push_or_decrease(static_cast<Key>(rng() % 1000000), v);

  std::vector<bool> seen(n + 1, false);
  Key prev = -1;
  Vertex count = 0;
  while (!heap.empty()) {
    const auto [k, v] = heap.pop();
    ASSERT_LE(prev, k);
    ASSERT_GE(v, 1u);
    ASSERT_LE(v, n);
    ASSERT_FALSE(seen[v]) << "vertex " << v << " popped twice";
    seen[v] = true;
    prev = k;
    ++count;
  }
  EXPECT_EQ(count, n);
}

// ---------------------------------------------------------------------------
// Contract assertions (Debug builds; assert is removed by NDEBUG in Release)
// ---------------------------------------------------------------------------

#ifndef NDEBUG

TEST(SegmentTreeHeapDeathTest, RejectsVertexZero) {
  db::SegmentTreeHeap heap(5);
  EXPECT_DEATH(heap.push_or_decrease(1, 0), "");
}

TEST(SegmentTreeHeapDeathTest, RejectsVertexPastN) {
  db::SegmentTreeHeap heap(5);
  EXPECT_DEATH(heap.push_or_decrease(1, 6), "");
}

TEST(SegmentTreeHeapDeathTest, PopOnEmptyAborts) {
  db::SegmentTreeHeap heap(5);
  EXPECT_DEATH(heap.pop(), "");
}

TEST(SegmentTreeHeapDeathTest, DecreaseMustBeStrictlySmaller) {
  db::SegmentTreeHeap heap(5);
  heap.push_or_decrease(5, 1);
  EXPECT_DEATH(heap.push_or_decrease(5, 1), "");
}

#endif // !NDEBUG

} // namespace
