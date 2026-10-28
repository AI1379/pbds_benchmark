// Shared Dijkstra loop, excerpted without identifying file-header comments.
// The caller allocates the distance vector and supplies a reusable empty heap.
template <typename Heap>
inline void dijkstra(const Graph &g, uint32_t s,
                     std::vector<int64_t> &dis, Heap &heap) {
  // Leave headroom for distance addition on the validated input domain.
  constexpr int64_t INF = std::numeric_limits<int64_t>::max() / 4;
  // Initialization is part of every timed search.
  std::fill(dis.begin(), dis.end(), INF);
  heap.push_or_decrease(0, s);
  dis[s] = 0;
  while (!heap.empty()) {
    auto [d, u] = heap.pop();
    // Standard-library lazy insertion can leave outdated entries.
    if (d > dis[u]) continue;
    // CSR adjacency traversal is shared by all heap variants.
    for (auto [v, w] : g[u]) {
      auto nd = dis[u] + w;
      if (dis[v] > nd) {
        dis[v] = nd;
        // Insert on first discovery, decrease an existing key otherwise.
        heap.push_or_decrease(dis[v], v);
      }
    }
  }
  // This implementation deliberately drains the heap, even for p2p input.
}
