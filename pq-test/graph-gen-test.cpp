#include "./dijkstra-heap.hpp"
#include "./dijkstra-pbds.hpp"
#include "./dijkstra-segt.hpp"
#include "./dijkstra-stl.hpp"
#include "./graph-generator.hpp"
using namespace std;
#define INIT_DIJKSTRA(nsp)            \
  nsp::graph = GraphGenerator::graph; \
  nsp::n = GraphGenerator::graph_n;   \
  nsp::m = GraphGenerator::graph_m;   \
  nsp::s = 1
#define OUTPUT_DIJKSTRA(nsp)          \
  for (int i = 1; i <= nsp::n; i++) { \
    cout << nsp::dis[i] << ' ';       \
  }                                   \
  cout << endl
#define RUN_DIJKSTRA(nsp) \
  INIT_DIJKSTRA(nsp);     \
  nsp::dijkstra();        \
  OUTPUT_DIJKSTRA(nsp)
int main() {
  GraphGenerator::randomWeightGrid(5, 5, make_pair(1, 20));
  GraphGenerator::addFlower(6, 8, make_pair(1, 20));
  GraphGenerator::addChain(4, 4, make_pair(1, 20));
  GraphGenerator::addRandomEdge(5, make_pair(1, 20));
  RUN_DIJKSTRA(dijkstra_pbds);
  RUN_DIJKSTRA(dijkstra_stl);
  RUN_DIJKSTRA(dijkstra_segt);
  RUN_DIJKSTRA(dijkstra_heap);

  return 0;
}