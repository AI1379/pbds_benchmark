#ifndef DIJKSTRA_STL_HPP
#define DIJKSTRA_STL_HPP

#include <bits/stdc++.h>

#include "graph-generator.hpp"

namespace dijkstra_stl {

using namespace std;
static ll n, m, s;
static ll dis[MAXN];
vector<pll> *graph;
priority_queue<pll, vector<pll>, greater<pll>> q;
void init() {
  for (ll i = 1; i <= n; i++) {
    dis[i] = INF;
  }
}
void dijkstra() {
  init();
  dis[s] = 0;
  q.push(make_pair(0, s));
  ll u, d, v;
  while (!q.empty()) {
    u = q.top().second;
    d = q.top().first;
    q.pop();
    if (d > dis[u]) continue;
    for (pll obj : graph[u]) {
      v = obj.first;
      d = obj.second;
      if (dis[v] > dis[u] + d) {
        dis[v] = dis[u] + d;
        q.push(make_pair(dis[v], v));
      }
    }
  }
}

}  // namespace dijkstra_stl

#endif