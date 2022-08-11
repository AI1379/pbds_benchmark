#ifndef SPFA_STL_HPP
#define SPFA_STL_HPP

#include <bits/stdc++.h>

#include "graph-generator.hpp"

namespace spfa_stl {

  using namespace std;
  static ll n, m, s;
  static ll dis[MAXN];
  static bool inqueue[MAXN];
  vector<pll> *graph;
  queue<ll> q;
  void init() {
    for (ll i = 1; i <= n; i++) {
      dis[i] = INF;
      inqueue[i] = false;
    }
  }
  // fake dijkstra
  // actually is spfa
  void dijkstra() {
    init();
    ll u, v, w;
    dis[s] = 0;
    inqueue[s] = true;
    q.push(s);
    while (!q.empty()) {
      u = q.front();
      q.pop();
      inqueue[u] = false;
      for (pll obj : graph[u]) {
        v  = obj.first;
        w = obj.second;
        if (dis[v] > dis[u] + w) {
          dis[v] = dis[u] + w;
          if (!inqueue[v]) {
            q.push(v);
            inqueue[v] = true;
          }
        }
      }
    }
  }
}  // namespace spfa_stl

#endif
