//
// dimacs-inspect: parse and validate a DIMACS shortest-path instance.
//
// This is the user-facing entry point of the DIMACS parser (dimacs-parser.hpp).
// It loads a `.gr` graph, optionally a `.co` coordinate file and a `.ss` query
// file, and prints a short summary with the parsed counts plus consistency
// statistics.  Parse errors are reported with `<file>:<line>: <problem>` and a
// non-zero exit code, which makes the tool usable in scripts / CI.
//
// Exit codes: 0 = success, 1 = parse or I/O error, 2 = command line error.

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "./dimacs-parser.hpp"

namespace {

using dijkstra_bench::DimacsCoordInfo;
using dijkstra_bench::DimacsCoordinate;
using dijkstra_bench::DimacsGraphInfo;
using dijkstra_bench::DimacsParseError;
using dijkstra_bench::DimacsParseOptions;
using dijkstra_bench::DimacsQuery;
using dijkstra_bench::DimacsQueryInfo;
using dijkstra_bench::Graph;

void print_usage(std::FILE *out) {
  std::fprintf(
      out,
      "Usage: dimacs-inspect [options] <graph.gr>\n"
      "\n"
      "Parse and validate a DIMACS 9th-challenge shortest-path instance.\n"
      "\n"
      "Options:\n"
      "  --coords <file.co>  Also parse/validate the vertex coordinates.\n"
      "  --queries <file>    Also parse/validate a query file: a `.p2p`\n"
      "                      point-to-point file (`p aux sp p2p` + `q S T`)\n"
      "                      or a `.ss` single-source file (`s S`).\n"
      "  --no-strict         Do not require the counts on the problem line to\n"
      "                      match the number of records in the file.\n"
      "  --check             Verify vertex coverage (count vertices that never\n"
      "                      appear in any arc).\n"
      "  --json              Print a machine-readable JSON summary.\n"
      "  -h, --help          Show this help text.\n"
      "\n"
      "Examples:\n"
      "  dimacs-inspect data/dimacs/USA-road-d.NY.gr\n"
      "  dimacs-inspect data/dimacs/USA-road-d.NY.gr "
      "--coords data/dimacs/USA-road-d.NY.co\n"
      "  dimacs-inspect data/dimacs/USA-road-d.NY.gr "
      "--queries data/dimacs/USA-road-d.NY.p2p\n");
}

std::string format_seconds(double seconds) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.3f", seconds);
  return buffer;
}

std::string format_number(double value, int precision) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
  return buffer;
}

std::string format_throughput(std::uint64_t items, double seconds) {
  if (seconds <= 0.0) {
    return "n/a";
  }
  return format_number(static_cast<double>(items) / seconds / 1e6, 2) +
         " M/s";
}

std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (const char c : text) {
    switch (c) {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

struct Options {
  std::filesystem::path graph;
  std::filesystem::path coords;
  std::filesystem::path queries;
  bool strict = true;
  bool check = false;
  bool json = false;
};

/// Returns false if the program should stop (help was requested or arguments
/// were invalid).  `exit_code` is set in the invalid-argument case.
bool parse_arguments(int argc, char **argv, Options &options, int &exit_code) {
  std::vector<std::string> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      print_usage(stdout);
      exit_code = 0;
      return false;
    }
    if (arg == "--coords" || arg == "--queries") {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "dimacs-inspect: missing value for %s\n",
                     std::string(arg).c_str());
        exit_code = 2;
        return false;
      }
      const std::filesystem::path value(argv[++i]);
      if (arg == "--coords") {
        options.coords = value;
      } else {
        options.queries = value;
      }
      continue;
    }
    if (arg == "--no-strict") {
      options.strict = false;
      continue;
    }
    if (arg == "--check") {
      options.check = true;
      continue;
    }
    if (arg == "--json") {
      options.json = true;
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      std::fprintf(stderr, "dimacs-inspect: unknown option '%.*s'\n",
                   static_cast<int>(arg.size()), arg.data());
      print_usage(stderr);
      exit_code = 2;
      return false;
    }
    positional.emplace_back(arg);
  }
  if (positional.size() != 1) {
    std::fprintf(stderr,
                 "dimacs-inspect: expected exactly one graph file, got %zu\n",
                 positional.size());
    print_usage(stderr);
    exit_code = 2;
    return false;
  }
  options.graph = positional.front();
  return true;
}

void print_graph_summary(const Graph &graph, const DimacsGraphInfo &info,
                         double seconds) {
  const double average_degree =
      graph.num_vertices() == 0
          ? 0.0
          : static_cast<double>(graph.num_edges()) / graph.num_vertices();
  std::printf("DIMACS graph: %s\n", info.path.string().c_str());
  std::printf("  problem line:              line %" PRIu64 " (p sp %u %" PRIu64
              ")\n",
              info.header_line, info.declared_vertices, info.declared_arcs);
  std::printf("  vertices:                  %u (declared %u)\n",
              graph.num_vertices(), info.declared_vertices);
  std::printf("  arcs:                      %zu (declared %" PRIu64 ")\n",
              graph.num_edges(), info.declared_arcs);
  std::printf("  max vertex id:             %u\n", info.max_vertex_id);
  std::printf("  self-loops:                %" PRIu64 "\n", info.self_loops);
  std::printf("  arc weight min/max/sum:    %" PRId32 " / %" PRId32
              " / %" PRIu64 "\n",
              info.min_weight, info.max_weight, info.weight_sum);
  std::printf("  out-degree min/avg/max:    %u / %s / %u\n",
              info.min_out_degree, format_number(average_degree, 5).c_str(),
              info.max_out_degree);
  if (info.vertex_coverage_checked) {
    std::printf("  vertices without arcs:     %" PRIu64 "\n",
                info.isolated_vertices);
  }
  std::printf("  parse time:                %s s (%s arcs)\n",
              format_seconds(seconds).c_str(),
              format_throughput(info.arcs, seconds).c_str());
}

void print_coord_summary(const DimacsCoordInfo &info, double seconds) {
  std::printf("DIMACS coordinates: %s\n", info.path.string().c_str());
  std::printf("  problem line:              line %" PRIu64 " (%s %u)\n",
              info.header_line,
              info.legacy_problem_line ? "p sp co" : "p aux sp co",
              info.declared_vertices);
  std::printf("  records:                   %" PRIu64 " (declared %u)\n",
              info.coordinates, info.declared_vertices);
  if (info.ranges_valid) {
    const double x_min = info.min_x / 1e6;
    const double x_max = info.max_x / 1e6;
    const double y_min = info.min_y / 1e6;
    const double y_max = info.max_y / 1e6;
    std::printf("  longitude range (deg):     %.6f .. %.6f\n", x_min, x_max);
    std::printf("  latitude  range (deg):     %.6f .. %.6f\n", y_min, y_max);
  }
  std::printf("  parse time:                %s s\n",
              format_seconds(seconds).c_str());
}

void print_query_summary(const DimacsQueryInfo &info, double seconds) {
  std::printf("DIMACS queries: %s\n", info.path.string().c_str());
  std::printf("  problem line:              line %" PRIu64
              " (p aux sp %s %" PRIu64 ")\n",
              info.header_line,
              info.kind == dijkstra_bench::DimacsQueryKind::single_source
                  ? "ss"
                  : "p2p",
              info.declared_queries);
  std::printf("  kind:                      %s\n",
              info.kind == dijkstra_bench::DimacsQueryKind::single_source
                  ? "single-source (.ss)"
                  : "point-to-point (.p2p)");
  std::printf("  queries:                   %" PRIu64 " (declared %" PRIu64
              ")\n",
              info.queries, info.declared_queries);
  if (info.kind == dijkstra_bench::DimacsQueryKind::point_to_point) {
    std::printf("  max source / target id:    %u / %u\n", info.max_source,
                info.max_target);
  } else {
    std::printf("  max source id:             %u\n", info.max_source);
  }
  if (info.vertices_checked != 0) {
    std::printf("  ids range-checked against: [1, %u]\n",
                info.vertices_checked);
  }
  std::printf("  parse time:                %s s (%s queries)\n",
              format_seconds(seconds).c_str(),
              format_throughput(info.queries, seconds).c_str());
}

void print_json(const Graph &graph, const DimacsGraphInfo &graph_info,
                double graph_seconds, const DimacsCoordInfo *coord_info,
                double coord_seconds, const DimacsQueryInfo *query_info,
                double query_seconds) {
  const double average_degree =
      graph.num_vertices() == 0
          ? 0.0
          : static_cast<double>(graph.num_edges()) / graph.num_vertices();
  std::printf("{\n");
  std::printf("  \"graph\": {\n");
  std::printf("    \"path\": \"%s\",\n",
              json_escape(graph_info.path.string()).c_str());
  std::printf("    \"declared_vertices\": %u,\n",
              graph_info.declared_vertices);
  std::printf("    \"declared_arcs\": %" PRIu64 ",\n",
              graph_info.declared_arcs);
  std::printf("    \"vertices\": %u,\n", graph.num_vertices());
  std::printf("    \"arcs\": %zu,\n", graph.num_edges());
  std::printf("    \"max_vertex_id\": %u,\n", graph_info.max_vertex_id);
  std::printf("    \"self_loops\": %" PRIu64 ",\n", graph_info.self_loops);
  std::printf("    \"weight_min\": %" PRId32 ",\n", graph_info.min_weight);
  std::printf("    \"weight_max\": %" PRId32 ",\n", graph_info.max_weight);
  std::printf("    \"weight_sum\": %" PRIu64 ",\n", graph_info.weight_sum);
  std::printf("    \"out_degree_min\": %u,\n", graph_info.min_out_degree);
  std::printf("    \"out_degree_max\": %u,\n", graph_info.max_out_degree);
  std::printf("    \"out_degree_average\": %s,\n",
              format_number(average_degree, 6).c_str());
  if (graph_info.vertex_coverage_checked) {
    std::printf("    \"isolated_vertices\": %" PRIu64 ",\n",
                graph_info.isolated_vertices);
  } else {
    std::printf("    \"isolated_vertices\": null,\n");
  }
  std::printf("    \"parse_seconds\": %s\n",
              format_number(graph_seconds, 6).c_str());
  if (coord_info != nullptr && coord_info->ranges_valid) {
    std::printf("  },\n");
    std::printf("  \"coordinates\": {\n");
    std::printf("    \"path\": \"%s\",\n",
                json_escape(coord_info->path.string()).c_str());
    std::printf("    \"declared_vertices\": %u,\n",
                coord_info->declared_vertices);
    std::printf("    \"records\": %" PRIu64 ",\n", coord_info->coordinates);
    std::printf("    \"min_x\": %" PRId32 ",\n", coord_info->min_x);
    std::printf("    \"max_x\": %" PRId32 ",\n", coord_info->max_x);
    std::printf("    \"min_y\": %" PRId32 ",\n", coord_info->min_y);
    std::printf("    \"max_y\": %" PRId32 ",\n", coord_info->max_y);
    std::printf("    \"legacy_problem_line\": %s,\n",
                coord_info->legacy_problem_line ? "true" : "false");
    std::printf("    \"parse_seconds\": %s\n",
                format_number(coord_seconds, 6).c_str());
  }
  if (query_info != nullptr) {
    std::printf("  },\n");
    std::printf("  \"queries\": {\n");
    std::printf("    \"path\": \"%s\",\n",
                json_escape(query_info->path.string()).c_str());
    std::printf("    \"kind\": \"%s\",\n",
                query_info->kind ==
                        dijkstra_bench::DimacsQueryKind::single_source
                    ? "ss"
                    : "p2p");
    std::printf("    \"declared_queries\": %" PRIu64 ",\n",
                query_info->declared_queries);
    std::printf("    \"queries\": %" PRIu64 ",\n", query_info->queries);
    std::printf("    \"max_source\": %u,\n", query_info->max_source);
    std::printf("    \"max_target\": %u,\n", query_info->max_target);
    std::printf("    \"vertices_checked\": %u,\n",
                query_info->vertices_checked);
    std::printf("    \"parse_seconds\": %s\n",
                format_number(query_seconds, 6).c_str());
  }
  std::printf("  }\n");
  std::printf("}\n");
}

int run(const Options &options) {
  DimacsParseOptions parse_options;
  parse_options.require_declared_counts = options.strict;
  parse_options.validate_vertex_coverage = options.check;

  const auto begin = std::chrono::steady_clock::now();
  DimacsGraphInfo graph_info;
  const Graph graph = dijkstra_bench::read_dimacs_gr(
      options.graph, parse_options, &graph_info);
  const double graph_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - begin)
          .count();

  DimacsCoordInfo coord_info;
  double coord_seconds = 0.0;
  if (!options.coords.empty()) {
    const auto coord_begin = std::chrono::steady_clock::now();
    dijkstra_bench::read_dimacs_co(options.coords, parse_options, &coord_info);
    coord_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - coord_begin)
                        .count();
    // The coordinate file must describe exactly the graph that was parsed.
    if (coord_info.declared_vertices != graph.num_vertices()) {
      throw DimacsParseError(coord_info.path.string() + ": declares " +
                             std::to_string(coord_info.declared_vertices) +
                             " vertices but " + options.graph.string() +
                             " has " + std::to_string(graph.num_vertices()));
    }
  }

  DimacsQueryInfo query_info;
  double query_seconds = 0.0;
  if (!options.queries.empty()) {
    const auto query_begin = std::chrono::steady_clock::now();
    dijkstra_bench::read_dimacs_queries(options.queries, graph.num_vertices(),
                                        parse_options, &query_info);
    query_seconds = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - query_begin)
                        .count();
  }

  if (options.json) {
    print_json(graph, graph_info, graph_seconds,
               options.coords.empty() ? nullptr : &coord_info, coord_seconds,
               options.queries.empty() ? nullptr : &query_info, query_seconds);
    return 0;
  }

  print_graph_summary(graph, graph_info, graph_seconds);
  if (!options.coords.empty()) {
    std::printf("\n");
    print_coord_summary(coord_info, coord_seconds);
  }
  if (!options.queries.empty()) {
    std::printf("\n");
    print_query_summary(query_info, query_seconds);
  }
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  Options options;
  int exit_code = 0;
  if (!parse_arguments(argc, argv, options, exit_code)) {
    return exit_code;
  }
  try {
    return run(options);
  } catch (const DimacsParseError &error) {
    std::fprintf(stderr, "dimacs-inspect: error: %s\n", error.what());
    return 1;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "dimacs-inspect: error: %s\n", error.what());
    return 1;
  }
}
