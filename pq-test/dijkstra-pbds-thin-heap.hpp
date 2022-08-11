#ifndef DIJKSTRA_PBDS_THIN_HEAP_HPP
#define DIJKSTRA_PBDS_THIN_HEAP_HPP

#include <bits/stdc++.h>
#include <ext/pb_ds/assoc_container.hpp>
#include <ext/pb_ds/priority_queue.hpp>


#include "graph-generator.hpp"

namespace dijkstra_pbds_thin_heap {

using namespace std;
static ll n, m, s;
static ll dis[MAXN];
vector<pll> *graph;
__gnu_pbds::priority_queue<pll, greater<pll>, __gnu_pbds::thin_heap_tag> q;
decltype(q)::point_iterator its[MAXN];
void init() {
  for (int i = 1; i <= n; i++) {
    dis[i] = INF;
  }
}
void dijkstra() {
  init();
  ll u, v, d;
  dis[s] = 0;
  for (int i = 1; i <= n; i++) {
    its[i] = q.push(make_pair(dis[i], i));
  }
  while (!q.empty()) {
    u = q.top().second;
    q.pop();
    for (pll obj : graph[u]) {
      v = obj.first;
      d = obj.second;
      if (dis[v] > dis[u] + d) {
        dis[v] = dis[u] + d;
        q.modify(its[v], make_pair(dis[u] + d, v));
      }
    }
  }
}

} // namespace dijkstra_pbds_thin_heap

#endif
