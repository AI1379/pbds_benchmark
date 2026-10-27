#ifndef GRAPH_GENERATOR
#define GRAPH_GENERATOR

#include <bits/stdc++.h>

using ll = long long;
using pll = std::pair<ll, ll>;
constexpr const ll MAXN = 200010;
constexpr const ll MAXM = 300010;
constexpr const ll INF = 1ll << (sizeof(ll) * 8 - 2);

namespace GraphGenerator {

using namespace std;
using Graph = vector<pll>;
using IntDist = uniform_int_distribution<ll>;
ll graph_n, graph_m;
Graph graph[MAXN];
map<pll, bool> vis;
mt19937 &getRandomEngine() {
  static random_device rd;
  static mt19937 gen(rd());
  return gen;
}
void printGraph(ostream &os) {
  os << graph_n << ' ' << graph_m << endl;
  for (int i = 1; i <= graph_n; i++) {
    for (auto v : graph[i]) {
      os << i << ' ' << v.first << ' ' << v.second << endl;
    }
  }
  os << endl;
}
void clearGraph() {
  for (int i = 1; i <= graph_n; i++) {
    graph[i].clear();
  }
  graph_n = 0;
  graph_m = 0;
  vis.clear();
}
void shuffleEdges() {
  for (int i = 1; i <= graph_n; i++) {
    shuffle(graph[i].begin(), graph[i].end(), getRandomEngine());
  }
}
void addEdge(ll u, ll v, ll w) {
  vis[make_pair(u, v)] = true;
  vis[make_pair(v, u)] = true;
  graph[u].emplace_back(make_pair(v, w));
  graph[v].emplace_back(make_pair(u, w));
}
void randomGraph(ll n, ll m, pll w_range, bool is_not_directed) {
  uniform_int_distribution<ll> vertex_dist(1, n);
  uniform_int_distribution<ll> weight_dist(w_range.first, w_range.second);
  if ((m > n * (n - 1) && !is_not_directed) ||
      (m > n * (n - 1) / 2 && is_not_directed)) {
    cerr << "ERROR: Too many edges" << endl;
    return;
  }
  ll u, v, w;
  graph_m = m;
  graph_n = n;
  for (int i = 0; i < m; ++i) {
    do {
      u = vertex_dist(getRandomEngine());
      v = vertex_dist(getRandomEngine());
    } while (vis[make_pair(u, v)] || u == v);
    w = weight_dist(getRandomEngine());
    vis[make_pair(u, v)] = true;
    graph[u].emplace_back(make_pair(v, w));
    if (is_not_directed) {
      vis[make_pair(v, u)] = true;
      graph[v].emplace_back(make_pair(u, w));
    }
  }
}

/// Generate a undirected grid graph
void randomWeightGrid(ll a, ll b, pll w_range) {
  graph_n = a * b;
  graph_m = 2 * a * b - a - b;
  IntDist weight_dist(w_range.first, w_range.second);
  ll now = 1, w;
  for (ll i = 1; i <= a; i++) {
    for (ll j = 1; j <= b; j++) {
      if (j != b) {
        w = weight_dist(getRandomEngine());
        addEdge(now, now + 1, w);
        // graph[now].emplace_back(make_pair(now + 1, w));
        // graph[now + 1].emplace_back(make_pair(now, w));
      }
      if (i != a) {
        w = weight_dist(getRandomEngine());
        addEdge(now, now + b, w);
        // graph[now].emplace_back(make_pair(now + b, w));
        // graph[now + b].emplace_back(make_pair(now, w));
      }
      ++now;
    }
  }
  shuffleEdges();
}

void addRandomEdge(ll m, pll w_range) {
  IntDist vertex_dist(1, graph_n);
  IntDist w_dist(w_range.first, w_range.second);
  ll u, v, w;
  for (int i = 1; i <= m; i++) {
    do {
      u = vertex_dist(getRandomEngine());
      v = vertex_dist(getRandomEngine());
    } while (vis[make_pair(u, v)]);
    w = w_dist(getRandomEngine());
    addEdge(u, v, w);
    // graph[u].emplace_back(make_pair(v, w));
    // graph[v].emplace_back(make_pair(u, w));
  }
  shuffleEdges();
  graph_m += m;
}

/// Link a Flower to a given node
void addFlower(ll node, ll n, pll w_range) {
  IntDist w_dist(w_range.first, w_range.second);
  ll w;
  for (ll i = 1; i <= n; i++) {
    w = w_dist(getRandomEngine());
    addEdge(node, graph_n + i, w);
    // graph[node].emplace_back(make_pair(graph_n + i, w));
    // graph[graph_n + i].emplace_back(make_pair(node, w));
  }
  shuffleEdges();
  graph_n += n;
  graph_m += n;
}

/// Link a Chain to a given node
void addChain(ll node, ll n, pll w_range) {
  IntDist w_dist(w_range.first, w_range.second);
  ll w = w_dist(getRandomEngine());
  addEdge(node, graph_n + 1, w);
  // graph[node].emplace_back(make_pair(graph_n + 1, w));
  // graph[graph_n + 1].emplace_back(make_pair(node, w));
  for (ll i = 2; i <= n; i++) {
    w = w_dist(getRandomEngine());
    addEdge(graph_n + i, graph_n + i - 1, w);
    // graph[graph_n + i].emplace_back(make_pair(graph_n + i - 1, w));
    // graph[graph_n + i - 1].emplace_back(make_pair(graph_n + i, w));
  }
  shuffleEdges();
  graph_n += n;
  graph_m += n;
}

void spfaKiller(ll n, ll wmax) {
  uniform_int_distribution<ll> w_dist(wmax / 2, wmax);
  auto point_to_skip = n + 3;
  if (n % 2 == 1) {
    point_to_skip = n / 2 + 1;
  }
  auto half = n / 2;
  ll x, y;
  for (int i = 1; i < half; ++i) {
    x = i;
    y = i + 1;
    addEdge(x + (x >= point_to_skip), y + (y >= point_to_skip),
            w_dist(getRandomEngine()));
    x = i + half;
    y = i + half + 1;
    addEdge(x + (x >= point_to_skip), y + (y >= point_to_skip),
            w_dist(getRandomEngine()));
    graph_m += 2;
  }
  for (int i = 1; i <= half; ++i) {
    x = i;
    y = i + half;
    addEdge(x + (x >= point_to_skip), y + (y >= point_to_skip),
            w_dist(getRandomEngine()));
    ++graph_m;
  }
  graph_n = n;
  // cout << "???" << endl;
  addRandomEdge(4, make_pair(wmax / 2, wmax));
}

} // namespace GraphGenerator

#endif