//
// Unit tests for the DIMACS shortest-path file parser (dimacs-parser.hpp).
//
// The tests build tiny DIMACS files in a temporary directory, so they do not
// depend on any downloaded dataset.  One extra integration test parses the
// real USA-road-d.NY instance when it is present under <proj2>/data/dimacs/
// (the CMake target passes its path through DIMACS_DATA_DIR); it is skipped
// otherwise.

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "./dimacs-parser.hpp"

namespace {

using dijkstra_bench::DimacsCoordInfo;
using dijkstra_bench::DimacsGraphInfo;
using dijkstra_bench::DimacsParseError;
using dijkstra_bench::DimacsParseOptions;
using dijkstra_bench::DimacsQueryInfo;
using dijkstra_bench::Graph;

/// Creates a unique temporary directory that is removed on destruction.
///
/// The directory is created next to the current working directory first and in
/// the system temp directory second: some CI/sandbox setups only allow the
/// test process to write inside the workspace, and in that case the system
/// temp directory is not writable.
class TempDir {
public:
  TempDir() {
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string name = "dimacs-parser-test-" + std::to_string(tick);
    std::error_code error;
    const std::filesystem::path candidates[] = {
        std::filesystem::current_path() / ".dimacs-parser-test" / name,
        std::filesystem::temp_directory_path() / name};
    for (const std::filesystem::path &candidate : candidates) {
      std::filesystem::create_directories(candidate, error);
      if (!error) {
        path_ = candidate;
        break;
      }
      error.clear();
    }
    if (path_.empty()) {
      throw std::runtime_error(
          "cannot create a temporary directory for the test");
    }
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
    // Remove the shared parent when it is empty; ignore failures.
    error.clear();
    std::filesystem::remove(path_.parent_path(), error);
  }

  TempDir(const TempDir &) = delete;
  TempDir &operator=(const TempDir &) = delete;

  const std::filesystem::path &path() const { return path_; }

  std::filesystem::path write(std::string_view name,
                              std::string_view content) const {
    const std::filesystem::path file = path_ / std::string(name);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    out.close();
    return file;
  }

private:
  std::filesystem::path path_;
};

/// Runs `action`, expecting a DimacsParseError; returns its message.
template <typename Action> std::string expect_parse_error(Action &&action) {
  try {
    action();
  } catch (const DimacsParseError &error) {
    return error.what();
  }
  ADD_FAILURE() << "expected DimacsParseError, but parsing succeeded";
  return {};
}

void expect_consistent(const Graph &graph) {
  const auto &offsets = graph.offsets();
  ASSERT_EQ(offsets.size(), static_cast<std::size_t>(graph.num_vertices()) + 1);
  ASSERT_EQ(offsets.front(), 0u);
  ASSERT_EQ(static_cast<std::size_t>(offsets.back()), graph.num_edges());
  for (std::size_t i = 0; i + 1 < offsets.size(); ++i) {
    EXPECT_LE(offsets[i], offsets[i + 1]);
  }
  for (std::uint32_t u = 1; u <= graph.num_vertices(); ++u) {
    for (const Graph::Edge &edge : graph[u]) {
      EXPECT_GE(edge.v, 1u);
      EXPECT_LE(edge.v, graph.num_vertices());
      EXPECT_GE(edge.w, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// .gr parsing
// ---------------------------------------------------------------------------

TEST(DimacsGr, ParsesSimpleGraphIntoCsr) {
  TempDir dir;
  const auto file = dir.write("simple.gr", R"(c a tiny graph
c second comment line
p sp 5 5
a 1 2 7
a 1 3 2
a 3 4 5
a 2 5 1
a 5 1 3
)");
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, {}, &info);

  EXPECT_EQ(graph.num_vertices(), 5u);
  EXPECT_EQ(graph.num_edges(), 5u);
  EXPECT_EQ(info.declared_vertices, 5u);
  EXPECT_EQ(info.declared_arcs, 5u);
  EXPECT_EQ(info.arcs, 5u);
  EXPECT_EQ(info.max_vertex_id, 5u);
  EXPECT_EQ(info.self_loops, 0u);
  EXPECT_EQ(info.min_weight, 1);
  EXPECT_EQ(info.max_weight, 7);
  EXPECT_EQ(info.weight_sum, 7u + 2u + 5u + 1u + 3u);
  EXPECT_EQ(info.min_out_degree, 0u);
  EXPECT_EQ(info.max_out_degree, 2u);
  EXPECT_EQ(info.header_line, 3u);
  EXPECT_EQ(info.path, file);
  expect_consistent(graph);

  const auto out1 = graph[1];
  ASSERT_EQ(out1.size(), 2u);
  EXPECT_EQ(out1[0].v, 2u);
  EXPECT_EQ(out1[0].w, 7);
  EXPECT_EQ(out1[1].v, 3u);
  EXPECT_EQ(out1[1].w, 2);
  EXPECT_TRUE(graph[4].empty());
  EXPECT_EQ(graph[5].size(), 1u);
  EXPECT_EQ(graph[5][0].v, 1u);
}

TEST(DimacsGr, HandlesCrlfBlankLinesTabsAndMissingFinalNewline) {
  TempDir dir;
  const auto file = dir.write("crlf.gr",
                              "c windows line endings\r\n"
                              "\r\n"
                              "  p sp 3 2\r\n"
                              "\tc indented comment\r\n"
                              " a 1\t2  4\r\n"
                              "\r\n"
                              "a 2 3 5"); // no trailing newline
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, {}, &info);
  EXPECT_EQ(graph.num_vertices(), 3u);
  EXPECT_EQ(graph.num_edges(), 2u);
  EXPECT_EQ(info.arcs, 2u);
  EXPECT_EQ(graph[1].size(), 1u);
  EXPECT_EQ(graph[1][0].w, 4);
  EXPECT_EQ(graph[2][0].v, 3u);
}

TEST(DimacsGr, SkipsUtf8ByteOrderMark) {
  TempDir dir;
  const auto file = dir.write("bom.gr", std::string("\xEF\xBB\xBF") +
                                           "p sp 1 1\n"
                                           "a 1 1 0\n");
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, {}, &info);
  EXPECT_EQ(graph.num_vertices(), 1u);
  EXPECT_EQ(graph.num_edges(), 1u);
  EXPECT_EQ(info.self_loops, 1u);
  EXPECT_EQ(info.min_weight, 0);
  EXPECT_EQ(info.max_weight, 0);
}

TEST(DimacsGr, HandlesLinesLongerThanTheReadBuffer) {
  TempDir dir;
  std::string content = "c " + std::string(std::size_t{2} << 20, 'x') + "\n";
  content += "p sp 2 1\na 1 2 9\n";
  const auto file = dir.write("long-line.gr", content);
  const Graph graph = dijkstra_bench::read_dimacs_gr(file);
  EXPECT_EQ(graph.num_vertices(), 2u);
  EXPECT_EQ(graph.num_edges(), 1u);
  EXPECT_EQ(graph[1][0].w, 9);
}

TEST(DimacsGr, ParsesEmptyArcSet) {
  TempDir dir;
  const auto file = dir.write("empty.gr", "p sp 3 0\n");
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, {}, &info);
  EXPECT_EQ(graph.num_vertices(), 3u);
  EXPECT_EQ(graph.num_edges(), 0u);
  EXPECT_EQ(info.min_out_degree, 0u);
  EXPECT_EQ(info.max_out_degree, 0u);
  EXPECT_EQ(info.min_weight, 0);
  EXPECT_EQ(info.max_weight, 0);
  EXPECT_EQ(info.weight_sum, 0u);
  expect_consistent(graph);
}

TEST(DimacsGr, ReportsVertexCoverageWhenRequested) {
  TempDir dir;
  const auto file = dir.write("coverage.gr", "p sp 3 1\na 1 2 4\n");
  DimacsParseOptions options;
  options.validate_vertex_coverage = true;
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, options, &info);
  EXPECT_EQ(graph.num_vertices(), 3u);
  EXPECT_TRUE(info.vertex_coverage_checked);
  EXPECT_EQ(info.isolated_vertices, 1u); // vertex 3 never appears

  options.require_vertex_coverage = true;
  const std::string message =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(file, options); });
  EXPECT_NE(message.find("never appear"), std::string::npos) << message;
}

TEST(DimacsGr, RejectsMissingProblemLine) {
  TempDir dir;
  const auto file = dir.write("no-header.gr", "c only comments\na 1 2 3\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find("before the problem line"), std::string::npos)
      << message;

  const auto empty = dir.write("empty-header.gr", "c nothing here\n");
  const std::string missing =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(empty); });
  EXPECT_NE(missing.find("missing 'p sp"), std::string::npos) << missing;
}

TEST(DimacsGr, RejectsProblemLineCountMismatch) {
  TempDir dir;
  const auto file = dir.write("mismatch.gr", "p sp 2 3\na 1 2 1\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find("declares 3 arcs"), std::string::npos) << message;
  EXPECT_NE(message.find("contains 1"), std::string::npos) << message;
}

TEST(DimacsGr, AllowsDeclaredCountMismatchWhenNotStrict) {
  TempDir dir;
  const auto file = dir.write("mismatch.gr", "p sp 2 3\na 1 2 1\n");
  DimacsParseOptions options;
  options.require_declared_counts = false;
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, options, &info);
  EXPECT_EQ(graph.num_edges(), 1u);
  EXPECT_EQ(info.declared_arcs, 3u);
  EXPECT_EQ(info.arcs, 1u);
}

TEST(DimacsGr, RejectsDuplicateProblemLine) {
  TempDir dir;
  const auto file =
      dir.write("dup-header.gr", "p sp 2 1\np sp 2 1\na 1 2 1\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find("duplicate problem line"), std::string::npos)
      << message;
}

TEST(DimacsGr, RejectsUnknownLineAndReportsLineNumber) {
  TempDir dir;
  const auto file = dir.write("unknown.gr",
                              "p sp 2 1\n"
                              "a 1 2 1\n"
                              "x this is not dimacs\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find(":3:"), std::string::npos) << message;
  EXPECT_NE(message.find("unexpected line"), std::string::npos) << message;
}

TEST(DimacsGr, RejectsTooManyVertices) {
  // The 32-bit per-vertex loops require n <= 2^32 - 2.
  TempDir dir;
  const auto file = dir.write("huge-n.gr", "p sp 4294967295 0\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find("too large"), std::string::npos) << message;
}

TEST(DimacsGr, RejectsOutOfRangeVertexIds) {
  TempDir dir;
  const auto head = dir.write("head.gr", "p sp 3 1\na 1 4 1\n");
  const std::string head_message =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(head); });
  EXPECT_NE(head_message.find("head vertex 4"), std::string::npos)
      << head_message;
  EXPECT_NE(head_message.find("outside [1, 3]"), std::string::npos)
      << head_message;

  const auto tail = dir.write("tail.gr", "p sp 3 1\na 0 2 1\n");
  const std::string tail_message =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(tail); });
  EXPECT_NE(tail_message.find("tail vertex 0"), std::string::npos)
      << tail_message;
}

TEST(DimacsGr, RejectsBadWeights) {
  TempDir dir;
  const auto negative = dir.write("negative.gr", "p sp 2 1\na 1 2 -5\n");
  const std::string negative_message =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(negative); });
  EXPECT_NE(negative_message.find("non-negative"), std::string::npos)
      << negative_message;

  const auto too_big =
      dir.write("too-big.gr", "p sp 2 1\na 1 2 2147483648\n");
  const std::string too_big_message =
      expect_parse_error([&] { dijkstra_bench::read_dimacs_gr(too_big); });
  EXPECT_NE(too_big_message.find("32-bit"), std::string::npos)
      << too_big_message;
}

TEST(DimacsGr, RejectsMalformedArcLines) {
  TempDir dir;
  const auto missing_weight = dir.write("short.gr", "p sp 2 1\na 1 2\n");
  const std::string short_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(missing_weight); });
  EXPECT_NE(short_message.find("missing weight"), std::string::npos)
      << short_message;

  const auto extra = dir.write("extra.gr", "p sp 2 1\na 1 2 3 4\n");
  const std::string extra_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(extra); });
  EXPECT_NE(extra_message.find("unexpected token '4'"), std::string::npos)
      << extra_message;

  const auto not_number = dir.write("not-number.gr", "p sp 2 1\na 1 two 3\n");
  const std::string number_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(not_number); });
  EXPECT_NE(number_message.find("invalid head vertex"), std::string::npos)
      << number_message;
}

TEST(DimacsGr, RejectsWrongProblemType) {
  TempDir dir;
  const auto file = dir.write("aux.gr", "p aux sp co 2\nv 1 0 0\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_gr(file); });
  EXPECT_NE(message.find("unsupported problem line"), std::string::npos)
      << message;
}

TEST(DimacsGr, ParsesManyArcsConsistently) {
  TempDir dir;
  constexpr std::uint32_t kVertices = 1000;
  std::vector<std::string> lines;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> arcs;
  for (std::uint32_t u = 1; u <= kVertices; ++u) {
    for (std::uint32_t k = 0; k < 5; ++k) {
      const std::uint32_t v = (u * 7 + k * 13) % kVertices + 1;
      const std::uint32_t w = (u + k) % 97;
      arcs.emplace_back(u, v);
      lines.push_back("a " + std::to_string(u) + " " + std::to_string(v) +
                      " " + std::to_string(w) + "\n");
    }
  }
  std::string content = "c generated graph\np sp " +
                        std::to_string(kVertices) + " " +
                        std::to_string(arcs.size()) + "\n";
  for (const std::string &line : lines) {
    content += line;
  }
  const auto file = dir.write("many.gr", content);
  DimacsGraphInfo info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(file, {}, &info);
  EXPECT_EQ(graph.num_vertices(), kVertices);
  EXPECT_EQ(graph.num_edges(), arcs.size());
  EXPECT_EQ(info.arcs, arcs.size());
  expect_consistent(graph);

  // The CSR order must be the file order, grouped by tail vertex.
  std::size_t position = 0;
  for (std::uint32_t u = 1; u <= kVertices; ++u) {
    for (const Graph::Edge &edge : graph[u]) {
      ASSERT_LT(position, arcs.size());
      EXPECT_EQ(arcs[position].first, u);
      EXPECT_EQ(arcs[position].second, edge.v);
      ++position;
    }
  }
  EXPECT_EQ(position, arcs.size());
}

// ---------------------------------------------------------------------------
// Query parsing: `.ss` (single-source) and `.p2p` (point-to-point)
// ---------------------------------------------------------------------------

TEST(DimacsSs, ParsesSingleSourceQueriesAndChecksRanges) {
  TempDir dir;
  const auto file = dir.write("queries.ss", "c queries\n"
                                           "p aux sp ss 3\n"
                                           "s 1\n"
                                           "s 4\n"
                                           "s 2\n");
  DimacsQueryInfo info;
  const auto sources = dijkstra_bench::read_dimacs_ss(file, 4, {}, &info);
  ASSERT_EQ(sources.size(), 3u);
  EXPECT_EQ(sources[0], 1u);
  EXPECT_EQ(sources[1], 4u);
  EXPECT_EQ(sources[2], 2u);
  EXPECT_EQ(info.kind, dijkstra_bench::DimacsQueryKind::single_source);
  EXPECT_EQ(info.declared_queries, 3u);
  EXPECT_EQ(info.queries, 3u);
  EXPECT_EQ(info.max_source, 4u);
  EXPECT_EQ(info.max_target, 0u);
  EXPECT_EQ(info.vertices_checked, 4u);

  const auto out_of_range =
      dir.write("bad.ss", "p aux sp ss 1\ns 5\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_ss(out_of_range, 4); });
  EXPECT_NE(message.find("source vertex 5"), std::string::npos) << message;
  EXPECT_NE(message.find("outside [1, 4]"), std::string::npos) << message;
}

TEST(DimacsSs, RejectsSingleSourceFileWithTargetId) {
  TempDir dir;
  const auto file = dir.write("ss-with-target.ss", "p aux sp ss 1\ns 1 2\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_ss(file); });
  EXPECT_NE(message.find("unexpected token '2'"), std::string::npos)
      << message;
}

TEST(DimacsSs, DoesNotRangeCheckWithoutVertexCount) {
  TempDir dir;
  const auto file = dir.write("queries.ss", "p aux sp ss 1\ns 999999\n");
  const auto sources = dijkstra_bench::read_dimacs_ss(file);
  ASSERT_EQ(sources.size(), 1u);
  EXPECT_EQ(sources[0], 999999u);
}

TEST(DimacsSs, RejectsVertexIdBeyondUint32EvenWithoutVertexCount) {
  TempDir dir;
  const auto file =
      dir.write("huge.ss", "p aux sp ss 1\ns 4294967296\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_ss(file); });
  EXPECT_NE(message.find("32-bit vertex id range"), std::string::npos)
      << message;
}

TEST(DimacsSs, RejectsDeclaredCountMismatch) {
  TempDir dir;
  const auto file = dir.write("queries.ss", "p aux sp ss 3\ns 1\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_ss(file); });
  EXPECT_NE(message.find("declares 3 queries"), std::string::npos) << message;
}

TEST(DimacsP2p, ParsesPointToPointQueriesAndChecksRanges) {
  TempDir dir;
  const auto file = dir.write("queries.p2p", "c p2p queries\n"
                                            "p aux sp p2p 2\n"
                                            "q 1 4\n"
                                            "q 2 3\n");
  DimacsQueryInfo info;
  const auto pairs = dijkstra_bench::read_dimacs_p2p(file, 4, {}, &info);
  ASSERT_EQ(pairs.size(), 2u);
  EXPECT_EQ(pairs[0].source, 1u);
  EXPECT_EQ(pairs[0].target, 4u);
  EXPECT_EQ(pairs[1].source, 2u);
  EXPECT_EQ(pairs[1].target, 3u);
  EXPECT_EQ(info.kind, dijkstra_bench::DimacsQueryKind::point_to_point);
  EXPECT_EQ(info.declared_queries, 2u);
  EXPECT_EQ(info.queries, 2u);
  EXPECT_EQ(info.max_source, 2u);
  EXPECT_EQ(info.max_target, 4u);

  const auto out_of_range =
      dir.write("bad.p2p", "p aux sp p2p 1\nq 1 5\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_p2p(out_of_range, 4); });
  EXPECT_NE(message.find("target vertex 5"), std::string::npos) << message;
  EXPECT_NE(message.find("outside [1, 4]"), std::string::npos) << message;
}

TEST(DimacsP2p, RejectsDeclaredCountMismatch) {
  TempDir dir;
  const auto file = dir.write("queries.p2p", "p aux sp p2p 3\nq 1 2\n");
  const std::string message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_p2p(file); });
  EXPECT_NE(message.find("declares 3 queries"), std::string::npos) << message;
}

TEST(DimacsQueries, AutoDetectsKindAndWrappersRejectWrongKind) {
  TempDir dir;
  const auto ss_file = dir.write("q.ss", "p aux sp ss 1\ns 1\n");
  const auto p2p_file = dir.write("q.p2p", "p aux sp p2p 1\nq 1 2\n");

  const auto ss = dijkstra_bench::read_dimacs_queries(ss_file);
  EXPECT_TRUE(ss.is_single_source());
  ASSERT_EQ(ss.sources.size(), 1u);
  EXPECT_TRUE(ss.pairs.empty());

  const auto p2p = dijkstra_bench::read_dimacs_queries(p2p_file);
  EXPECT_FALSE(p2p.is_single_source());
  EXPECT_TRUE(p2p.sources.empty());
  ASSERT_EQ(p2p.pairs.size(), 1u);

  // Reading a p2p file through the .ss wrapper (or vice versa) must fail with
  // an explanatory message instead of silently misparsing.
  const std::string ss_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_ss(p2p_file); });
  EXPECT_NE(ss_message.find("single-source (.ss)"), std::string::npos)
      << ss_message;
  const std::string p2p_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_p2p(ss_file); });
  EXPECT_NE(p2p_message.find("point-to-point (.p2p)"), std::string::npos)
      << p2p_message;
}

// ---------------------------------------------------------------------------
// .co parsing
// ---------------------------------------------------------------------------

TEST(DimacsCo, ParsesCoordinates) {
  TempDir dir;
  const auto file = dir.write("coords.co", "c coords\n"
                                          "p aux sp co 3\n"
                                          "v 1 -73530767 41085396\n"
                                          "v 2 -73530538 41086098\n"
                                          "v 3 -73000000 40000000\n");
  DimacsCoordInfo info;
  const auto coordinates = dijkstra_bench::read_dimacs_co(file, {}, &info);
  ASSERT_EQ(coordinates.size(), 3u);
  EXPECT_EQ(coordinates[0].x, -73530767);
  EXPECT_EQ(coordinates[0].y, 41085396);
  EXPECT_EQ(coordinates[1].x, -73530538);
  EXPECT_EQ(coordinates[2].y, 40000000);
  EXPECT_EQ(info.declared_vertices, 3u);
  EXPECT_EQ(info.coordinates, 3u);
  EXPECT_EQ(info.min_x, -73530767);
  EXPECT_EQ(info.max_x, -73000000);
  EXPECT_EQ(info.min_y, 40000000);
  EXPECT_EQ(info.max_y, 41086098);
  EXPECT_TRUE(info.ranges_valid);
  EXPECT_FALSE(info.legacy_problem_line);
}

TEST(DimacsCo, AcceptsLegacyProblemLine) {
  // The official benchmark package's own sample coordinate file uses the old
  // "p sp co <n>" spelling instead of "p aux sp co <n>".
  TempDir dir;
  const auto file = dir.write("legacy.co", "c legacy sample header\n"
                                           "p sp co 2\n"
                                           "v 1 10 20\n"
                                           "v 2 30 40\n");
  DimacsCoordInfo info;
  const auto coordinates = dijkstra_bench::read_dimacs_co(file, {}, &info);
  ASSERT_EQ(coordinates.size(), 2u);
  EXPECT_EQ(coordinates[0].x, 10);
  EXPECT_EQ(coordinates[0].y, 20);
  EXPECT_EQ(coordinates[1].x, 30);
  EXPECT_EQ(coordinates[1].y, 40);
  EXPECT_EQ(info.declared_vertices, 2u);
  EXPECT_EQ(info.coordinates, 2u);
  EXPECT_TRUE(info.legacy_problem_line);
}

TEST(DimacsCo, RejectsBadCoordinateFiles) {
  TempDir dir;
  const auto out_of_range =
      dir.write("bad-id.co", "p aux sp co 2\nv 3 0 0\n");
  const std::string id_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_co(out_of_range); });
  EXPECT_NE(id_message.find("vertex id 3"), std::string::npos) << id_message;

  const auto duplicate = dir.write(
      "dup.co", "p aux sp co 2\nv 1 0 0\nv 1 1 1\n");
  const std::string dup_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_co(duplicate); });
  EXPECT_NE(dup_message.find("duplicate coordinate"), std::string::npos)
      << dup_message;

  const auto missing = dir.write("missing.co", "p aux sp co 2\nv 1 0 0\n");
  const std::string missing_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_co(missing); });
  EXPECT_NE(missing_message.find("contains 1 coordinate"), std::string::npos)
      << missing_message;

  const auto wrong_type = dir.write("wrong.co", "p aux sp ss 2\ns 1 2\n");
  const std::string type_message = expect_parse_error(
      [&] { dijkstra_bench::read_dimacs_co(wrong_type); });
  EXPECT_NE(type_message.find("p aux sp co"), std::string::npos)
      << type_message;
}

// ---------------------------------------------------------------------------
// Real dataset integration (optional, skipped when data is not downloaded)
// ---------------------------------------------------------------------------

#ifdef DIMACS_DATA_DIR
namespace {
const std::filesystem::path kDataDir = DIMACS_DATA_DIR;
} // namespace

TEST(DimacsIntegration, ParsesNyRoadNetwork) {
  const std::filesystem::path graph_path = kDataDir / "USA-road-d.NY.gr";
  if (!std::filesystem::exists(graph_path)) {
    GTEST_SKIP() << "dataset not downloaded: " << graph_path;
  }
  DimacsParseOptions options;
  options.validate_vertex_coverage = true;
  DimacsGraphInfo info;
  const Graph graph =
      dijkstra_bench::read_dimacs_gr(graph_path, options, &info);
  EXPECT_EQ(graph.num_vertices(), 264346u);
  EXPECT_EQ(graph.num_edges(), 733846u);
  EXPECT_EQ(info.declared_vertices, 264346u);
  EXPECT_EQ(info.declared_arcs, 733846u);
  EXPECT_EQ(info.arcs, 733846u);
  EXPECT_EQ(info.max_vertex_id, 264346u);
  EXPECT_EQ(info.self_loops, 0u);
  EXPECT_EQ(info.min_weight, 1);
  EXPECT_EQ(info.max_weight, 36946);
  EXPECT_EQ(info.isolated_vertices, 0u);
  EXPECT_TRUE(info.vertex_coverage_checked);
  expect_consistent(graph);
}

TEST(DimacsIntegration, ParsesNyCoordinates) {
  const std::filesystem::path coord_path = kDataDir / "USA-road-d.NY.co";
  if (!std::filesystem::exists(coord_path)) {
    GTEST_SKIP() << "dataset not downloaded: " << coord_path;
  }
  DimacsCoordInfo info;
  const auto coordinates =
      dijkstra_bench::read_dimacs_co(coord_path, {}, &info);
  EXPECT_EQ(coordinates.size(), 264346u);
  EXPECT_EQ(info.coordinates, 264346u);
  EXPECT_TRUE(info.ranges_valid);
  // USA-road-d.NY covers the New York City metropolitan area, roughly
  // longitude -74.5..-73.5 and latitude 40.4..41.3 degrees.
  EXPECT_LT(info.min_x, -74'000'000);
  EXPECT_GT(info.max_x, -73'600'000);
  EXPECT_LT(info.min_y, 40'500'000);
  EXPECT_GT(info.max_y, 41'200'000);
}

TEST(DimacsIntegration, ParsesOfficialSampleQueryFiles) {
  const std::filesystem::path samples = kDataDir / "samples";
  const std::filesystem::path ss_path = samples / "sample.ss";
  if (!std::filesystem::exists(ss_path)) {
    GTEST_SKIP() << "official sample files not downloaded: " << ss_path;
  }
  DimacsQueryInfo ss_info;
  const auto sources =
      dijkstra_bench::read_dimacs_ss(ss_path, 0, {}, &ss_info);
  EXPECT_EQ(ss_info.kind, dijkstra_bench::DimacsQueryKind::single_source);
  EXPECT_EQ(sources.size(), static_cast<std::size_t>(ss_info.declared_queries));
  EXPECT_FALSE(sources.empty());

  const std::filesystem::path p2p_path = samples / "sample.p2p";
  if (std::filesystem::exists(p2p_path)) {
    DimacsQueryInfo p2p_info;
    const auto pairs =
        dijkstra_bench::read_dimacs_p2p(p2p_path, 0, {}, &p2p_info);
    EXPECT_EQ(p2p_info.kind, dijkstra_bench::DimacsQueryKind::point_to_point);
    EXPECT_EQ(pairs.size(),
              static_cast<std::size_t>(p2p_info.declared_queries));
    EXPECT_FALSE(pairs.empty());
  }

  const std::filesystem::path sample_graph = samples / "sample.gr";
  if (std::filesystem::exists(sample_graph)) {
    DimacsParseOptions options;
    options.validate_vertex_coverage = true;
    DimacsGraphInfo graph_info;
    const Graph graph =
        dijkstra_bench::read_dimacs_gr(sample_graph, options, &graph_info);
    EXPECT_EQ(graph.num_vertices(), graph_info.declared_vertices);
    EXPECT_EQ(graph.num_edges(),
              static_cast<std::size_t>(graph_info.declared_arcs));
    expect_consistent(graph);
  }

  const std::filesystem::path sample_coords = samples / "sample.co";
  if (std::filesystem::exists(sample_coords)) {
    DimacsCoordInfo coord_info;
    const auto coordinates =
        dijkstra_bench::read_dimacs_co(sample_coords, {}, &coord_info);
    ASSERT_EQ(coordinates.size(), 6u);
    EXPECT_EQ(coordinates[0].x, 10);
    EXPECT_EQ(coordinates[0].y, 20);
    EXPECT_EQ(coord_info.declared_vertices, 6u);
  }
}
#endif

} // namespace
