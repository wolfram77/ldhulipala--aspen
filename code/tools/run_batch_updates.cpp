#include "../graph/api.h"
#include "../trees/utils.h"
#include "../lib_extensions/sparse_table_hash.h"
#include "../pbbslib/random_shuffle.h"

#include <cstring>

#include <vector>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>
#include <iostream>
#include <fstream>
#include <random>
#include <tuple>

#include "rmat_util.h"

using namespace std;
using edge_seq = pair<uintV, uintV>;
using edge_upd = tuple<uintV, uintV>;




// Get current time.
inline auto timeNow() {
  return chrono::high_resolution_clock::now();
}


// Get time duration, in milliseconds.
template <class T>
inline float duration(const T& start, const T& stop) {
  auto a = chrono::duration_cast<chrono::microseconds>(stop - start);
  return a.count() / 1000.0f;
}


// Generate edge deletions.
inline vector<edge_upd> generateEdgeDeletions(versioned_graph<treeplus_graph>& VG, int batchSize, bool isSymmetric) {
  random_device dev;
  default_random_engine rnd(dev());
  int retries = 5;
  auto S = VG.acquire_version();
  int  n = S.graph.num_vertices();
  uniform_int_distribution<int> dist(0, n-1);
  vector<edge_upd> deletions;
  for (int b=0; b<batchSize; ++b) {
    for (int r=0; r<retries; ++r) {
      int   u = dist(rnd);
      auto it = S.graph.find_vertex(u);
      int degree = it.value.degree();
      if (degree == 0) continue;
      int   j = dist(rnd) % degree;
      auto fm = [&](auto _, auto v) {
        if (j-- != 0) return;
        deletions.push_back({u, v});
        if (isSymmetric) deletions.push_back({v, u});
      };
      it.value.map_nghs(u, fm);
      break;
    }
  }
  VG.release_version(move(S));
  return deletions;
}


// Generate edge insertions.
inline vector<edge_upd> generateEdgeInsertions(versioned_graph<treeplus_graph>& VG, int batchSize, bool isSymmetric) {
  random_device dev;
  default_random_engine rnd(dev());
  int retries = 5;
  auto S = VG.acquire_version();
  int  n = S.graph.num_vertices();
  uniform_int_distribution<int> dist(0, n-1);
  vector<edge_upd> insertions;
  for (int b=0; b<batchSize; ++b) {
    for (int r=0; r<retries; ++r) {
      int u = dist(rnd);
      int v = dist(rnd);
      auto it = S.graph.find_vertex(u);
      bool found = false;
      auto fm = [&](auto _, auto w) {
        if (w==v) found = true;
      };
      it.value.map_nghs(u, fm);
      if (found) continue;
      insertions.push_back({u, v});
      if (isSymmetric) insertions.push_back({v, u});
      break;
    }
  }
  VG.release_version(move(S));
  return insertions;
}


// Test graph transpose.
inline void testGraphTranspose(versioned_graph<treeplus_graph>& VG) {
  versioned_graph<treeplus_graph> VT;
  auto S = VG.acquire_version();
  auto G = S.graph;
  size_t m = G.num_edges();
  printf("Transposing graph...\n");
  auto t0 = timeNow();
  auto edges = G.retrieve_edges();
  auto fs = [&](auto i) { swap(get<0>(edges[i]), get<1>(edges[i])); };
  parallel_for(0, m, fs);
  VT.insert_edges_batch(m, edges.begin(), false, false);
  auto t3 = timeNow();
  printf("Time to transpose graph: %.2f ms\n", duration(t0, t3));
  S.graph.clear_root();
  VG.release_version(move(S));
}


// Test multi-step visit count with BFS, from all vertices.
inline void testVisitCountBfs(treeplus_graph& G, int steps) {
  printf("Testing visit count with BFS [%d steps]...\n", steps);
  size_t n = G.num_vertices();
  vector<size_t> visits0(n, 1);
  vector<size_t> visits1(n, 0);
  auto t0 = timeNow();
  for (int s=0; s<steps; ++s) {
    #pragma omp parallel for schedule(static, 2048)
    for (size_t i=0; i<n; ++i)
      visits1[i] = 0;
    auto fm = [&](auto u, auto v) {
      visits1[u] += visits0[v];
    };
    G.map_all_edges(fm);
    swap(visits0, visits1);
  }
  auto t1 = timeNow();
  size_t total = 0;
  for (size_t i=0; i<n; ++i)
    total += visits0[i];
  printf("Total visits: %zu\n", total);
  printf("Time to visit count: %.2f ms\n", duration(t0, t1));
}




// Main function.
int main(int argc, char** argv) {
  char *file = argv[1];
  bool symmetric = argc>2? atoi(argv[2]) : false;
  bool weighted  = argc>3? atoi(argv[3]) : false;
  bool mmap      = argc>4? atoi(argv[4]) : false;
  int  visits    = argc>5? atoi(argv[5]) : 42;
  printf("NUM_WORKERS=%d\n", num_workers());
  // Load graph from adjacency graph format file.
  auto  t0 = timeNow();
  auto  VG = initialize_graph(file, mmap, symmetric, false, 1);
  auto  t1 = timeNow();
  auto   S = VG.acquire_version();
  size_t n = S.graph.num_vertices();
  size_t m = S.graph.num_edges();
  printf("Nodes: %zu, Edges: %zu\n", n, m);
  printf("Time to load graph: %.2f ms\n", duration(t0, t1));
  VG.release_version(move(S));
  // Test graph transpose.
  testGraphTranspose(VG);
  printf("\n");
  // Perform batch updates of varying sizes.
  for (int batchPower=-7; batchPower<=-1; ++batchPower) {
    double batchFraction = pow(10.0, batchPower);
    int    batchSize     = (int) round(batchFraction * m);
    printf("Batch fraction: %.1e [%d edges]\n", batchFraction, batchSize);
    // Perform edge deletions.
    {
      vector<edge_upd> deletions = generateEdgeDeletions(VG, batchSize, symmetric);
      auto t0 = timeNow();
      auto  S = VG.acquire_version();
      auto t1 = timeNow();
      printf("Nodes: %zu, Edges: %zu\n", S.graph.num_vertices(), S.graph.num_edges());
      printf("Time to acquire version: %.2f ms\n", duration(t0, t1));
      printf("Deleting edges [%zu edges]...\n", deletions.size());
      auto t2 = timeNow();
      auto  G = S.graph.delete_edges_batch(deletions.size(), deletions.data(), false, true);
      auto t3 = timeNow();
      printf("Nodes: %zu, Edges: %zu\n", G.num_vertices(), G.num_edges());
      printf("Time to delete edges: %.2f ms\n", duration(t2, t3));
      for (auto [u, v] : deletions) {
        auto it = G.find_vertex(u);
        bool found = false;
        auto fm = [&](auto _, auto w) {
          if (w==v) found = true;
        };
        it.value.map_nghs(u, fm);
        assert(!found);
      }
      testVisitCountBfs(G, visits);
      G.clear_root();
      S.graph.clear_root();
      VG.release_version(move(S));
    }
    // Perform edge insertions.
    {
      vector<edge_upd> insertions = generateEdgeInsertions(VG, batchSize, symmetric);
      auto t0 = timeNow();
      auto  S = VG.acquire_version();
      auto t1 = timeNow();
      printf("Nodes: %zu, Edges: %zu\n", S.graph.num_vertices(), S.graph.num_edges());
      printf("Time to acquire version: %.2f ms\n", duration(t0, t1));
      printf("Inserting edges [%zu edges]...\n", insertions.size());
      auto t2 = timeNow();
      auto  G = S.graph.insert_edges_batch(insertions.size(), insertions.data(), false, true);
      auto t3 = timeNow();
      printf("Nodes: %zu, Edges: %zu\n", G.num_vertices(), G.num_edges());
      printf("Time to insert edges: %.2f ms\n", duration(t2, t3));
      for (auto [u, v] : insertions) {
        auto it = G.find_vertex(u);
        bool found = false;
        auto fm = [&](auto _, auto w) {
          if (w==v) found = true;
        };
        it.value.map_nghs(u, fm);
        assert(found);
      }
      testVisitCountBfs(G, visits);
      G.clear_root();
      S.graph.clear_root();
    }
    printf("\n");
  }
  printf("\n");
  return 0;
}
