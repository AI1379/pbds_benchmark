#ifndef DIJKSTRA_SEGT_HPP
#define DIJKSTRA_SEGT_HPP

#include <bits/stdc++.h>

#include "graph-generator.hpp"

namespace dijkstra_segt {

using namespace std;
static ll n, m, s;
static ll dis[MAXN];
vector<pll> *graph;
struct Node {
  ll l, r, val, pos;
};
static Node segt[MAXN * 4];
ll cmp(ll lhs, ll rhs) { return dis[lhs] < dis[rhs] ? lhs : rhs; }
void build(ll beg, ll end, ll root) {
  if (beg == end) {
    segt[root].val = dis[beg];
    segt[root].pos = beg;
    segt[root].l = beg;
    segt[root].r = end;
    return;
  }
  ll mid = (beg + end) / 2;
  build(beg, mid, root * 2);
  build(mid + 1, end, root * 2 + 1);
  if (segt[root * 2].val < segt[root * 2 + 1].val) {
    segt[root].val = segt[root * 2].val;
    segt[root].pos = segt[root * 2].pos;
  } else {
    segt[root].val = segt[root * 2 + 1].val;
    segt[root].pos = segt[root * 2 + 1].pos;
  }
  segt[root].l = beg;
  segt[root].r = end;
}
void modify(ll pos, ll root, ll val) {
  if (segt[root].l == pos && segt[root].r == pos) {
    segt[root].val = val;
    return;
  }
  ll mid = (segt[root].l + segt[root].r) / 2;
  if (pos <= mid) {
    modify(pos, root * 2, val);
  } else {
    modify(pos, root * 2 + 1, val);
  }
  if (segt[root * 2].val < segt[root * 2 + 1].val) {
    segt[root].val = segt[root * 2].val;
    segt[root].pos = segt[root * 2].pos;
  } else {
    segt[root].val = segt[root * 2 + 1].val;
    segt[root].pos = segt[root * 2 + 1].pos;
  }
}
void init() {
  for (int i = 1; i <= n; i++) {
    dis[i] = INF;
  }
}
void dijkstra() {
  ll u, v, w;
  init();
  dis[s] = 0;
  build(1, n, 1);
  while (segt[1].val != INF) {
    u = segt[1].pos;
    modify(u, 1, INF);
    for (pll obj : graph[u]) {
      v = obj.first;
      w = obj.second;
      if (dis[v] > dis[u] + w) {
        dis[v] = dis[u] + w;
        modify(v, 1, dis[v]);
      }
    }
  }
}

}  // namespace dijkstra_segt

#endif