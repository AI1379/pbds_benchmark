//
// DIMACS shortest-path file parser.
//
// The 9th DIMACS Implementation Challenge (Shortest Paths) distributes its
// benchmark graphs as plain text files in the so-called DIMACS format:
//
//   * graph file (`.gr` for the challenge road networks):
//        c <any comment>                 0..* comment lines
//        p sp <n> <m>                    problem line: n vertices, m arcs
//        a <u> <v> <w>                   arc u -> v with weight w
//   * coordinate file (`.co`):
//        p aux sp co <n>                 n vertices follow
//        v <id> <longitude> <latitude>   coordinates in 1e-6 degree units
//   * query files, two flavours:
//     - single-source (`.ss`):
//        p aux sp ss <k>                 k sources follow
//        s <source>
//     - point-to-point (`.p2p`), what the project's 1000 query pairs use:
//        p aux sp p2p <k>                k source-target pairs follow
//        q <source> <target>
//
// Vertex ids are 1-based.  The weights of the road-network instances are
// non-negative 32-bit integers.
//
// This header turns a `.gr` file into a `dijkstra_bench::Graph` (CSR layout,
// see csr-graph.hpp) using two streaming passes over the file:
//
//   pass 1: validate the header/lines, count the out-degree of every vertex and
//           collect lightweight statistics;
//   pass 2: fill the CSR arc array.
//
// Therefore the peak memory usage is roughly
//     m * sizeof(Graph::Edge) + (n + 1) * 2 * sizeof(uint32_t)
//     (+ (n + 1) bytes when vertex coverage is checked)
// With the current 16-byte `Graph::Edge` this is about 16*m + 8*n bytes
// (NY: ~14 MB, E: ~169 MB).  The file itself is never loaded into memory at
// once, so arbitrarily large instances are fine as long as their 32-bit CSR
// representation fits into RAM.
//
// All syntax/semantic problems raise DimacsParseError.  Line-level problems
// carry a `<file>:<line>: <problem>` message; file-level problems (missing
// problem line, declared-count mismatch, ...) carry `<file>: <problem>`.

#pragma once
#ifndef DIMACS_PARSER_HPP
#define DIMACS_PARSER_HPP

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "./csr-graph.hpp"

namespace dijkstra_bench {

/// Raised for every malformed / inconsistent DIMACS input.
class DimacsParseError : public std::runtime_error {
public:
  explicit DimacsParseError(std::string message)
      : std::runtime_error(std::move(message)) {}
};

/// Knobs shared by the three readers below.
struct DimacsParseOptions {
  /// Fail when the number of records in the file differs from the count
  /// declared on the problem line.
  bool require_declared_counts = true;
  /// Count vertices that never appear in any arc (`DimacsGraphInfo::
  /// isolated_vertices`).  Costs one extra byte per vertex.
  bool validate_vertex_coverage = false;
  /// Fail if a declared vertex never appears in any arc (implies
  /// validate_vertex_coverage).
  bool require_vertex_coverage = false;
};

/// Statistics about a parsed `.gr` file (all vertex ids are the original
/// 1-based DIMACS ids).
struct DimacsGraphInfo {
  std::filesystem::path path;
  std::uint64_t header_line = 0;
  std::uint32_t declared_vertices = 0;
  std::uint64_t declared_arcs = 0;
  std::uint64_t arcs = 0;
  std::uint32_t max_vertex_id = 0;
  std::uint64_t self_loops = 0;
  std::int32_t min_weight = 0;
  std::int32_t max_weight = 0;
  std::uint64_t weight_sum = 0;
  std::uint32_t min_out_degree = 0;
  std::uint32_t max_out_degree = 0;
  std::uint64_t isolated_vertices = 0;
  bool vertex_coverage_checked = false;
};

/// Kind of a DIMACS query file.
///
/// The 9th DIMACS challenge defines two query-file flavours (see
/// `docs/format/format.shtml` of the challenge package):
///   * `.ss`  - single-source: `p aux sp ss <n>` + `s <source>` lines;
///   * `.p2p` - point-to-point: `p aux sp p2p <n>` + `q <source> <target>`.
enum class DimacsQueryKind {
  single_source,
  point_to_point,
};

/// One source-target pair of a `.p2p` query file (1-based vertex ids).
struct DimacsQuery {
  std::uint32_t source = 0;
  std::uint32_t target = 0;
};

/// A parsed query file.  Exactly one of `sources` / `pairs` is populated,
/// depending on `kind`.
struct DimacsQueryFile {
  DimacsQueryKind kind = DimacsQueryKind::single_source;
  std::vector<std::uint32_t> sources; ///< `.ss`: 1-based source ids
  std::vector<DimacsQuery> pairs;     ///< `.p2p`: source-target pairs

  bool is_single_source() const noexcept {
    return kind == DimacsQueryKind::single_source;
  }
};

struct DimacsQueryInfo {
  std::filesystem::path path;
  DimacsQueryKind kind = DimacsQueryKind::single_source;
  std::uint64_t header_line = 0;
  std::uint64_t declared_queries = 0;
  std::uint64_t queries = 0;
  std::uint32_t max_source = 0;
  /// 0 for single-source files.
  std::uint32_t max_target = 0;
  /// Vertex count used to validate the ids, 0 if none was given.
  std::uint32_t vertices_checked = 0;
};

/// One `.co` coordinate record.  DIMACS stores longitude/latitude as integers
/// in units of 1e-6 degrees (WGS 84), so a value of -73530767 means -73.530767.
struct DimacsCoordinate {
  std::int32_t x = 0; ///< longitude * 1e6
  std::int32_t y = 0; ///< latitude * 1e6
};

struct DimacsCoordInfo {
  std::filesystem::path path;
  std::uint64_t header_line = 0;
  std::uint32_t declared_vertices = 0;
  std::uint64_t coordinates = 0;
  std::int32_t min_x = 0;
  std::int32_t max_x = 0;
  std::int32_t min_y = 0;
  std::int32_t max_y = 0;
  bool ranges_valid = false; ///< false when the file contains no coordinates
  /// True when the problem line used the legacy 'p sp co <n>' spelling.
  bool legacy_problem_line = false;
};

// ---------------------------------------------------------------------------
// Implementation details
// ---------------------------------------------------------------------------
namespace dimacs_detail {

[[noreturn]] inline void fail(const std::filesystem::path &path,
                              std::uint64_t line, std::string_view what) {
  std::string message = path.string();
  if (line != 0) {
    message += ':';
    message += std::to_string(line);
  }
  message += ": ";
  message += what;
  throw DimacsParseError(std::move(message));
}

inline std::string_view trim_left(std::string_view text) noexcept {
  std::size_t begin = 0;
  while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\t')) {
    ++begin;
  }
  return text.substr(begin);
}

inline bool is_blank(std::string_view text) noexcept {
  for (const char c : text) {
    if (c != ' ' && c != '\t') {
      return false;
    }
  }
  return true;
}

/// Simple whitespace separated tokenizer for one (already trimmed) line.
class Tokenizer {
public:
  explicit Tokenizer(std::string_view line) noexcept : line_(line) {}

  bool next(std::string_view &token) noexcept {
    while (pos_ < line_.size() &&
           (line_[pos_] == ' ' || line_[pos_] == '\t')) {
      ++pos_;
    }
    if (pos_ == line_.size()) {
      return false;
    }
    const std::size_t begin = pos_;
    while (pos_ < line_.size() && line_[pos_] != ' ' && line_[pos_] != '\t') {
      ++pos_;
    }
    token = line_.substr(begin, pos_ - begin);
    return true;
  }

private:
  std::string_view line_;
  std::size_t pos_ = 0;
};

inline std::uint64_t to_u64(const std::filesystem::path &path,
                            std::uint64_t line, std::string_view token,
                            std::string_view what) {
  std::uint64_t value = 0;
  const char *const first = token.data();
  const char *const last = first + token.size();
  const std::from_chars_result result = std::from_chars(first, last, value);
  if (result.ec == std::errc::result_out_of_range) {
    fail(path, line, std::string(what) + " is out of range: '" +
                         std::string(token) + "'");
  }
  if (result.ec != std::errc() || result.ptr != last) {
    fail(path, line, std::string("invalid ") + std::string(what) + ": '" +
                         std::string(token) + "'");
  }
  return value;
}

inline std::int64_t to_i64(const std::filesystem::path &path,
                           std::uint64_t line, std::string_view token,
                           std::string_view what) {
  std::int64_t value = 0;
  const char *const first = token.data();
  const char *const last = first + token.size();
  const std::from_chars_result result = std::from_chars(first, last, value);
  if (result.ec == std::errc::result_out_of_range) {
    fail(path, line, std::string(what) + " is out of range: '" +
                         std::string(token) + "'");
  }
  if (result.ec != std::errc() || result.ptr != last) {
    fail(path, line, std::string("invalid ") + std::string(what) + ": '" +
                         std::string(token) + "'");
  }
  return value;
}

/// Buffered line reader.  `next()` yields one line without the trailing `\n`
/// (and without a trailing `\r`, so CRLF files work).  The returned view stays
/// valid only until the next call to `next()`.
///
/// Lines longer than the internal buffer are handled through a carry buffer,
/// so no input line length is assumed.
class LineReader {
public:
  explicit LineReader(const std::filesystem::path &path,
                      std::size_t buffer_size = std::size_t{1} << 20)
      : stream_(path, std::ios::binary), path_(path), buffer_(buffer_size) {
    if (!stream_.is_open()) {
      fail(path, 0, "cannot open file for reading");
    }
  }

  bool next(std::string_view &line) {
    // The previously returned view may point into `carry_`; only recycle the
    // buffer now that the caller is done with it.  (`clear()` writes the
    // terminating NUL, so it must not run while a returned view is still in
    // use.)
    if (carry_consumed_) {
      carry_.clear();
      carry_consumed_ = false;
    }
    for (;;) {
      if (pos_ == end_ && !fill()) {
        if (carry_.empty()) {
          return false;
        }
        line = std::string_view(carry_.data(), carry_.size());
        carry_consumed_ = true;
        ++line_number_;
        strip_cr(line);
        return true;
      }
      const char *const base = buffer_.data() + pos_;
      const std::size_t available = end_ - pos_;
      const void *const newline = std::memchr(base, '\n', available);
      if (newline != nullptr) {
        const std::size_t length =
            static_cast<const char *>(newline) - base;
        if (carry_.empty()) {
          line = std::string_view(base, length);
        } else {
          carry_.append(base, length);
          line = std::string_view(carry_.data(), carry_.size());
          carry_consumed_ = true;
        }
        pos_ += length + 1;
        ++line_number_;
        strip_cr(line);
        return true;
      }
      carry_.append(base, available);
      pos_ = end_;
    }
  }

  std::uint64_t line_number() const noexcept { return line_number_; }

private:
  bool fill() {
    stream_.read(buffer_.data(),
                 static_cast<std::streamsize>(buffer_.size()));
    const std::streamsize got = stream_.gcount();
    if (got <= 0) {
      if (stream_.bad()) {
        // A real I/O error must not be silently treated as end-of-file.
        fail(path_, 0, "I/O error while reading file");
      }
      return false;
    }
    pos_ = 0;
    end_ = static_cast<std::size_t>(got);
    // Skip a UTF-8 byte order mark on the very first chunk (some mirrors
    // re-encode the ASCII files with a BOM).
    if (line_number_ == 0 && carry_.empty() && end_ >= 3 &&
        static_cast<unsigned char>(buffer_[0]) == 0xEF &&
        static_cast<unsigned char>(buffer_[1]) == 0xBB &&
        static_cast<unsigned char>(buffer_[2]) == 0xBF) {
      pos_ = 3;
    }
    return true;
  }

  static void strip_cr(std::string_view &line) noexcept {
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
  }

  std::ifstream stream_;
  std::filesystem::path path_;
  std::vector<char> buffer_;
  std::size_t pos_ = 0;
  std::size_t end_ = 0;
  std::string carry_;
  bool carry_consumed_ = false;
  std::uint64_t line_number_ = 0;
};

struct GrHeader {
  std::uint32_t n = 0;
  std::uint64_t m = 0;
  std::uint64_t line = 0;
};

inline GrHeader parse_gr_header(const std::filesystem::path &path,
                                std::uint64_t line_number,
                                std::string_view line) {
  Tokenizer tokens(line);
  std::string_view token;
  if (!tokens.next(token) || token != "p") {
    fail(path, line_number,
         "expected 'p sp <vertices> <arcs>' problem line");
  }
  if (!tokens.next(token) || token != "sp") {
    fail(path, line_number,
         "unsupported problem line, expected 'p sp <vertices> <arcs>'");
  }
  if (!tokens.next(token)) {
    fail(path, line_number, "missing vertex count on problem line");
  }
  const std::uint64_t n = to_u64(path, line_number, token, "vertex count");
  if (!tokens.next(token)) {
    fail(path, line_number, "missing arc count on problem line");
  }
  const std::uint64_t m = to_u64(path, line_number, token, "arc count");
  if (tokens.next(token)) {
    fail(path, line_number,
         "unexpected token '" + std::string(token) + "' after problem line");
  }
  constexpr std::uint64_t kMaxU32 = std::numeric_limits<std::uint32_t>::max();
  if (n == 0) {
    fail(path, line_number, "problem line declares 0 vertices");
  }
  if (n > kMaxU32) {
    fail(path, line_number,
         "vertex count exceeds the 32-bit vertex id range");
  }
  if (n == kMaxU32) {
    // The per-vertex loops and the CSR prefix sums are 32-bit; requiring
    // n <= 2^32 - 2 keeps every `++u` loop free of unsigned wrap-around.
    fail(path, line_number,
         "vertex count " + std::to_string(n) +
             " is too large for this implementation (max is " +
             std::to_string(kMaxU32 - 1) + ")");
  }
  if (m > kMaxU32) {
    fail(path, line_number,
         "arc count exceeds the 32-bit CSR offset range");
  }
  return {static_cast<std::uint32_t>(n), m, line_number};
}

struct ArcLine {
  std::uint32_t u = 0;
  std::uint32_t v = 0;
  std::int32_t w = 0;
};

inline ArcLine parse_arc_line(const std::filesystem::path &path,
                              std::uint64_t line_number,
                              std::string_view line, std::uint32_t n) {
  Tokenizer tokens(line);
  std::string_view token;
  if (!tokens.next(token) || token != "a") {
    fail(path, line_number, "expected 'a <tail> <head> <weight>' arc line");
  }
  if (!tokens.next(token)) {
    fail(path, line_number, "missing tail vertex on arc line");
  }
  const std::uint64_t u = to_u64(path, line_number, token, "tail vertex");
  if (!tokens.next(token)) {
    fail(path, line_number, "missing head vertex on arc line");
  }
  const std::uint64_t v = to_u64(path, line_number, token, "head vertex");
  if (!tokens.next(token)) {
    fail(path, line_number, "missing weight on arc line");
  }
  const std::int64_t w = to_i64(path, line_number, token, "arc weight");
  if (tokens.next(token)) {
    fail(path, line_number,
         "unexpected token '" + std::string(token) + "' after arc line");
  }
  if (u < 1 || u > n) {
    fail(path, line_number,
         "tail vertex " + std::to_string(u) + " is outside [1, " +
             std::to_string(n) + "]");
  }
  if (v < 1 || v > n) {
    fail(path, line_number,
         "head vertex " + std::to_string(v) + " is outside [1, " +
             std::to_string(n) + "]");
  }
  if (w < 0) {
    fail(path, line_number,
         "arc weight must be non-negative, got " + std::to_string(w));
  }
  if (w > std::numeric_limits<std::int32_t>::max()) {
    fail(path, line_number,
         "arc weight " + std::to_string(w) +
             " exceeds the 32-bit range of DIMACS arc weights");
  }
  return {static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v),
          static_cast<std::int32_t>(w)};
}

/// Header of the auxiliary DIMACS files: 'p aux sp <kind> <count>'.
struct AuxHeader {
  std::uint64_t count = 0;
  std::uint64_t line = 0;
  /// True when the file used the legacy 'p <kind> ...' spelling without the
  /// 'aux' marker (only `.co` files have such a legacy variant).
  bool legacy = false;
};

/// Header of a `.co` coordinate file.
///
/// The official format page defines `p aux sp co <n>`, but the challenge's own
/// sample coordinate file (`docs/format/samples/sample.co.txt` inside the
/// official benchmark package) still uses the older `p sp co <n>` spelling.
/// Both are accepted; the strict `.gr`-style spelling is only a legacy variant
/// of the same auxiliary format.
inline AuxHeader parse_co_header(const std::filesystem::path &path,
                                 std::uint64_t line_number,
                                 std::string_view line) {
  Tokenizer tokens(line);
  std::string_view token;
  if (!tokens.next(token) || token != "p") {
    fail(path, line_number,
         "expected a 'p aux sp co <n>' (or legacy 'p sp co <n>') problem "
         "line");
  }
  if (!tokens.next(token)) {
    fail(path, line_number,
         "expected a 'p aux sp co <n>' (or legacy 'p sp co <n>') problem "
         "line");
  }
  bool legacy = true;
  if (token == "aux") {
    legacy = false;
    if (!tokens.next(token)) {
      fail(path, line_number,
           "expected a 'p aux sp co <n>' (or legacy 'p sp co <n>') problem "
           "line");
    }
  }
  if (token != "sp") {
    fail(path, line_number,
         "expected a 'p aux sp co <n>' (or legacy 'p sp co <n>') problem "
         "line");
  }
  if (!tokens.next(token) || token != "co") {
    fail(path, line_number,
         "expected a 'p aux sp co <n>' (or legacy 'p sp co <n>') problem "
         "line");
  }
  if (!tokens.next(token)) {
    fail(path, line_number, "missing vertex count on problem line");
  }
  const std::uint64_t count = to_u64(path, line_number, token, "vertex count");
  if (tokens.next(token)) {
    fail(path, line_number,
         "unexpected token '" + std::string(token) + "' after problem line");
  }
  return {count, line_number, legacy};
}

inline void require_no_extra_tokens(const std::filesystem::path &path,
                                    std::uint64_t line,
                                    Tokenizer &tokens) {
  std::string_view token;
  if (tokens.next(token)) {
    fail(path, line,
         "unexpected token '" + std::string(token) + "' at end of line");
  }
}

/// Header of a DIMACS query file: 'p aux sp <ss|p2p> <count>'.
struct QueryHeader {
  DimacsQueryKind kind = DimacsQueryKind::single_source;
  std::uint64_t count = 0;
  std::uint64_t line = 0;
};

inline const char *query_kind_name(DimacsQueryKind kind) noexcept {
  return kind == DimacsQueryKind::single_source ? "single-source (.ss)"
                                                : "point-to-point (.p2p)";
}

inline QueryHeader parse_query_header(const std::filesystem::path &path,
                                      std::uint64_t line_number,
                                      std::string_view line) {
  Tokenizer tokens(line);
  std::string_view token;
  if (!tokens.next(token) || token != "p") {
    fail(path, line_number,
         "expected a 'p aux sp <ss|p2p> <queries>' problem line");
  }
  if (!tokens.next(token) || token != "aux") {
    fail(path, line_number,
         "expected a 'p aux sp <ss|p2p> <queries>' problem line");
  }
  if (!tokens.next(token) || token != "sp") {
    fail(path, line_number,
         "expected a 'p aux sp <ss|p2p> <queries>' problem line");
  }
  if (!tokens.next(token)) {
    fail(path, line_number,
         "missing query kind ('ss' or 'p2p') on problem line");
  }
  DimacsQueryKind kind;
  if (token == "ss") {
    kind = DimacsQueryKind::single_source;
  } else if (token == "p2p") {
    kind = DimacsQueryKind::point_to_point;
  } else {
    fail(path, line_number,
         "unsupported query kind '" + std::string(token) +
             "' (expected 'ss' or 'p2p')");
  }
  if (!tokens.next(token)) {
    fail(path, line_number, "missing query count on problem line");
  }
  const std::uint64_t count = to_u64(path, line_number, token, "query count");
  if (tokens.next(token)) {
    fail(path, line_number,
         "unexpected token '" + std::string(token) + "' after problem line");
  }
  return {kind, count, line_number};
}

} // namespace dimacs_detail

// ---------------------------------------------------------------------------
// Public readers
// ---------------------------------------------------------------------------

/// Read a DIMACS `.gr` shortest-path graph into CSR form.
///
/// Throws DimacsParseError on the first malformed line, a missing/duplicate
/// problem line, out-of-range vertex ids, negative weights, or (with
/// `options.require_declared_counts`) an arc count that differs from the one
/// declared on the problem line.
///
/// `info`, when non-null, receives dataset statistics; they are filled in even
/// if parsing fails?  No -- like the returned graph, they are only stored after
/// the whole file has been validated.
inline Graph read_dimacs_gr(const std::filesystem::path &path,
                            DimacsParseOptions options = {},
                            DimacsGraphInfo *info = nullptr) {
  DimacsGraphInfo stats;
  stats.path = path;

  // ---- pass 1: header, out-degrees, statistics -------------------------
  std::vector<std::uint32_t> out_degree; // index by 1-based tail vertex
  std::vector<std::uint8_t> touched;     // only when coverage is requested
  dimacs_detail::GrHeader header;
  bool have_header = false;
  std::uint64_t arcs = 0;
  std::uint64_t self_loops = 0;
  std::uint64_t weight_sum = 0;
  const bool check_coverage =
      options.validate_vertex_coverage || options.require_vertex_coverage;
  {
    dimacs_detail::LineReader reader(path);
    std::string_view line;
    bool have_weight = false;
    std::int32_t min_weight = 0;
    std::int32_t max_weight = 0;
    while (reader.next(line)) {
      const std::string_view trimmed = dimacs_detail::trim_left(line);
      if (dimacs_detail::is_blank(trimmed)) {
        continue;
      }
      switch (trimmed.front()) {
      case 'c': // comment line
        continue;
      case 'p': {
        if (have_header) {
          dimacs_detail::fail(path, reader.line_number(),
                              "duplicate problem line");
        }
        header = dimacs_detail::parse_gr_header(path, reader.line_number(),
                                                trimmed);
        have_header = true;
        out_degree.assign(
            static_cast<std::size_t>(header.n) + 1, std::uint32_t{0});
        if (check_coverage) {
          touched.assign(static_cast<std::size_t>(header.n) + 1,
                         std::uint8_t{0});
        }
        continue;
      }
      case 'a': {
        if (!have_header) {
          dimacs_detail::fail(path, reader.line_number(),
                              "arc line appears before the problem line");
        }
        const dimacs_detail::ArcLine arc =
            dimacs_detail::parse_arc_line(path, reader.line_number(), trimmed,
                                          header.n);
        ++out_degree[arc.u];
        ++arcs;
        if (arc.u == arc.v) {
          ++self_loops;
        }
        if (!have_weight) {
          min_weight = arc.w;
          max_weight = arc.w;
          have_weight = true;
        } else {
          min_weight = std::min(min_weight, arc.w);
          max_weight = std::max(max_weight, arc.w);
        }
        if (weight_sum > std::numeric_limits<std::uint64_t>::max() -
                             static_cast<std::uint64_t>(arc.w)) {
          dimacs_detail::fail(path, reader.line_number(),
                              "sum of arc weights overflows uint64");
        }
        weight_sum += static_cast<std::uint64_t>(arc.w);
        stats.max_vertex_id =
            std::max({stats.max_vertex_id, arc.u, arc.v});
        if (!touched.empty()) {
          touched[arc.u] = 1;
          touched[arc.v] = 1;
        }
        continue;
      }
      default:
        dimacs_detail::fail(
            path, reader.line_number(),
            "unexpected line (expected a 'c', 'p' or 'a' line)");
      }
    }
    stats.min_weight = have_weight ? min_weight : 0;
    stats.max_weight = have_weight ? max_weight : 0;
  }

  if (!have_header) {
    dimacs_detail::fail(path, 0,
                        "missing 'p sp <vertices> <arcs>' problem line");
  }
  if (options.require_declared_counts && arcs != header.m) {
    dimacs_detail::fail(
        path, 0,
        "problem line declares " + std::to_string(header.m) +
            " arcs but the file contains " + std::to_string(arcs));
  }
  if (arcs > std::numeric_limits<std::uint32_t>::max()) {
    dimacs_detail::fail(path, 0,
                        "too many arcs for a 32-bit CSR layout (" +
                            std::to_string(arcs) + ")");
  }

  stats.header_line = header.line;
  stats.declared_vertices = header.n;
  stats.declared_arcs = header.m;
  stats.arcs = arcs;
  stats.self_loops = self_loops;
  stats.weight_sum = weight_sum;

  // Vertex coverage / out-degree statistics.
  std::uint32_t min_out_degree = std::numeric_limits<std::uint32_t>::max();
  std::uint32_t max_out_degree = 0;
  for (std::uint32_t u = 1; u <= header.n; ++u) {
    min_out_degree = std::min(min_out_degree, out_degree[u]);
    max_out_degree = std::max(max_out_degree, out_degree[u]);
    if (!touched.empty() && touched[u] == 0) {
      ++stats.isolated_vertices;
    }
  }
  stats.min_out_degree = min_out_degree;
  stats.max_out_degree = max_out_degree;
  stats.vertex_coverage_checked = !touched.empty();
  if (options.require_vertex_coverage && stats.isolated_vertices != 0) {
    dimacs_detail::fail(path, 0,
                        std::to_string(stats.isolated_vertices) +
                            " vertices never appear in an arc");
  }

  // Prefix sums: out_degree[u] becomes the CSR offset (cumulative arc count
  // of vertices 1..u), which is exactly what Graph expects.
  for (std::uint32_t u = 1; u <= header.n; ++u) {
    out_degree[u] += out_degree[u - 1];
  }

  // ---- pass 2: fill the arc array --------------------------------------
  std::vector<Graph::Edge> edges(static_cast<std::size_t>(arcs));
  std::vector<std::uint32_t> cursor(static_cast<std::size_t>(header.n) + 1,
                                    std::uint32_t{0});
  for (std::uint32_t u = 1; u <= header.n; ++u) {
    cursor[u] = out_degree[u - 1];
  }
  {
    dimacs_detail::LineReader reader(path);
    std::string_view line;
    std::uint64_t filled = 0;
    while (reader.next(line)) {
      const std::string_view trimmed = dimacs_detail::trim_left(line);
      if (dimacs_detail::is_blank(trimmed) || trimmed.front() != 'a') {
        continue;
      }
      const dimacs_detail::ArcLine arc = dimacs_detail::parse_arc_line(
          path, reader.line_number(), trimmed, header.n);
      std::uint32_t &position = cursor[arc.u];
      if (position >= out_degree[arc.u]) {
        dimacs_detail::fail(
            path, reader.line_number(),
            "internal error: arc counts changed between parsing passes "
            "(file modified while reading?)");
      }
      edges[position] = Graph::Edge{arc.v, arc.w};
      ++position;
      ++filled;
    }
    if (filled != arcs) {
      dimacs_detail::fail(
          path, 0,
          "file changed while parsing: " + std::to_string(arcs) +
              " arcs were counted in the first pass but " +
              std::to_string(filled) + " were read in the second pass");
    }
  }
  for (std::uint32_t u = 1; u <= header.n; ++u) {
    if (cursor[u] != out_degree[u]) {
      dimacs_detail::fail(
          path, 0,
          "internal error: CSR fill did not complete for vertex " +
              std::to_string(u));
    }
  }

  if (info != nullptr) {
    *info = std::move(stats);
  }
  return Graph(std::move(out_degree), std::move(edges));
}

namespace dimacs_detail {

/// Shared implementation of the query-file readers.  `expected`, when set,
/// forces one particular flavour (`.ss` or `.p2p`) and reports a clear error if
/// the file is of the other kind.
inline DimacsQueryFile read_query_file(
    const std::filesystem::path &path, std::uint32_t num_vertices,
    const DimacsParseOptions &options, DimacsQueryInfo &stats,
    std::optional<DimacsQueryKind> expected) {
  stats.path = path;
  stats.vertices_checked = num_vertices;

  DimacsQueryFile file;
  bool have_header = false;
  std::uint64_t declared = 0;
  std::uint64_t header_line = 0;

  const auto check_id = [num_vertices](const std::filesystem::path &file_path,
                                       std::uint64_t line_number,
                                       std::uint64_t id,
                                       std::string_view what) {
    if (id > std::numeric_limits<std::uint32_t>::max()) {
      fail(file_path, line_number,
           std::string(what) + " " + std::to_string(id) +
               " exceeds the 32-bit vertex id range");
    }
    if (num_vertices != 0 && (id < 1 || id > num_vertices)) {
      fail(file_path, line_number,
           std::string(what) + " " + std::to_string(id) + " is outside [1, " +
               std::to_string(num_vertices) + "]");
    }
  };

  {
    LineReader reader(path);
    std::string_view line;
    while (reader.next(line)) {
      const std::string_view trimmed = trim_left(line);
      if (is_blank(trimmed)) {
        continue;
      }
      switch (trimmed.front()) {
      case 'c':
        continue;
      case 'p': {
        if (have_header) {
          fail(path, reader.line_number(), "duplicate problem line");
        }
        const QueryHeader header =
            parse_query_header(path, reader.line_number(), trimmed);
        if (expected.has_value() && header.kind != *expected) {
          fail(path, header.line,
               std::string("expected a ") + query_kind_name(*expected) +
                   " query file but the problem line declares " +
                   query_kind_name(header.kind));
        }
        file.kind = header.kind;
        declared = header.count;
        header_line = header.line;
        have_header = true;
        continue;
      }
      case 's': {
        if (!have_header) {
          fail(path, reader.line_number(),
               "query line appears before the problem line");
        }
        if (file.kind != DimacsQueryKind::single_source) {
          fail(path, reader.line_number(),
               "expected 'q <source> <target>' lines in a point-to-point "
               "(.p2p) query file");
        }
        Tokenizer tokens(trimmed);
        std::string_view token;
        if (!tokens.next(token) || token != "s") {
          fail(path, reader.line_number(), "expected 's <source>' source line");
        }
        if (!tokens.next(token)) {
          fail(path, reader.line_number(),
               "missing source vertex on source line");
        }
        const std::uint64_t source =
            to_u64(path, reader.line_number(), token, "source vertex");
        require_no_extra_tokens(path, reader.line_number(), tokens);
        check_id(path, reader.line_number(), source, "source vertex");
        file.sources.push_back(static_cast<std::uint32_t>(source));
        stats.max_source =
            std::max(stats.max_source, static_cast<std::uint32_t>(source));
        continue;
      }
      case 'q': {
        if (!have_header) {
          fail(path, reader.line_number(),
               "query line appears before the problem line");
        }
        if (file.kind != DimacsQueryKind::point_to_point) {
          fail(path, reader.line_number(),
               "expected 's <source>' lines in a single-source (.ss) query "
               "file");
        }
        Tokenizer tokens(trimmed);
        std::string_view token;
        if (!tokens.next(token) || token != "q") {
          fail(path, reader.line_number(),
               "expected 'q <source> <target>' query line");
        }
        if (!tokens.next(token)) {
          fail(path, reader.line_number(),
               "missing source vertex on query line");
        }
        const std::uint64_t source =
            to_u64(path, reader.line_number(), token, "source vertex");
        if (!tokens.next(token)) {
          fail(path, reader.line_number(),
               "missing target vertex on query line");
        }
        const std::uint64_t target =
            to_u64(path, reader.line_number(), token, "target vertex");
        require_no_extra_tokens(path, reader.line_number(), tokens);
        check_id(path, reader.line_number(), source, "source vertex");
        check_id(path, reader.line_number(), target, "target vertex");
        file.pairs.push_back({static_cast<std::uint32_t>(source),
                              static_cast<std::uint32_t>(target)});
        stats.max_source =
            std::max(stats.max_source, static_cast<std::uint32_t>(source));
        stats.max_target =
            std::max(stats.max_target, static_cast<std::uint32_t>(target));
        continue;
      }
      default:
        fail(path, reader.line_number(),
             "unexpected line (expected a 'c', 'p', 's' or 'q' line)");
      }
    }
  }

  if (!have_header) {
    fail(path, 0, "missing 'p aux sp <ss|p2p> <queries>' problem line");
  }
  stats.kind = file.kind;
  stats.header_line = header_line;
  stats.declared_queries = declared;
  stats.queries = file.sources.size() + file.pairs.size();
  if (options.require_declared_counts && stats.queries != declared) {
    fail(path, 0, "problem line declares " + std::to_string(declared) +
                      " queries but the file contains " +
                      std::to_string(stats.queries));
  }
  return file;
}

} // namespace dimacs_detail

/// Read either a `.ss` (single-source) or `.p2p` (point-to-point) DIMACS query
/// file.  The flavour is detected from the problem line and reported through
/// `DimacsQueryInfo::kind`; the matching field of the result is populated.
inline DimacsQueryFile
read_dimacs_queries(const std::filesystem::path &path,
                    std::uint32_t num_vertices = 0,
                    DimacsParseOptions options = {},
                    DimacsQueryInfo *info = nullptr) {
  DimacsQueryInfo stats;
  DimacsQueryFile file = dimacs_detail::read_query_file(
      path, num_vertices, options, stats, std::nullopt);
  if (info != nullptr) {
    *info = std::move(stats);
  }
  return file;
}

/// Read a single-source `.ss` query file; returns the 1-based source ids.
/// Throws if the file is not of the single-source flavour.
inline std::vector<std::uint32_t>
read_dimacs_ss(const std::filesystem::path &path,
               std::uint32_t num_vertices = 0,
               DimacsParseOptions options = {},
               DimacsQueryInfo *info = nullptr) {
  DimacsQueryInfo stats;
  DimacsQueryFile file = dimacs_detail::read_query_file(
      path, num_vertices, options, stats,
      DimacsQueryKind::single_source);
  if (info != nullptr) {
    *info = std::move(stats);
  }
  return std::move(file.sources);
}

/// Read a point-to-point `.p2p` query file; returns the source-target pairs.
/// Throws if the file is not of the point-to-point flavour.
inline std::vector<DimacsQuery>
read_dimacs_p2p(const std::filesystem::path &path,
                std::uint32_t num_vertices = 0,
                DimacsParseOptions options = {},
                DimacsQueryInfo *info = nullptr) {
  DimacsQueryInfo stats;
  DimacsQueryFile file = dimacs_detail::read_query_file(
      path, num_vertices, options, stats,
      DimacsQueryKind::point_to_point);
  if (info != nullptr) {
    *info = std::move(stats);
  }
  return std::move(file.pairs);
}

/// Read a DIMACS `.co` coordinate file.
///
/// The returned vector has `declared_vertices` entries; entry `id - 1` belongs
/// to DIMACS vertex `id`.  Coordinates are kept in the original integer unit
/// (1e-6 degree).
inline std::vector<DimacsCoordinate>
read_dimacs_co(const std::filesystem::path &path,
               DimacsParseOptions options = {},
               DimacsCoordInfo *info = nullptr) {
  DimacsCoordInfo stats;
  stats.path = path;

  std::vector<DimacsCoordinate> coordinates;
  std::vector<std::uint8_t> seen;
  bool have_header = false;
  std::uint32_t declared = 0;
  bool ranges_valid = false;
  {
    dimacs_detail::LineReader reader(path);
    std::string_view line;
    while (reader.next(line)) {
      const std::string_view trimmed = dimacs_detail::trim_left(line);
      if (dimacs_detail::is_blank(trimmed)) {
        continue;
      }
      switch (trimmed.front()) {
      case 'c':
        continue;
      case 'p': {
        if (have_header) {
          dimacs_detail::fail(path, reader.line_number(),
                              "duplicate problem line");
        }
        const dimacs_detail::AuxHeader header =
            dimacs_detail::parse_co_header(path, reader.line_number(), trimmed);
        if (header.count == 0 ||
            header.count > std::numeric_limits<std::uint32_t>::max()) {
          dimacs_detail::fail(
              path, reader.line_number(),
              "coordinate file declares an invalid vertex count: " +
                  std::to_string(header.count));
        }
        declared = static_cast<std::uint32_t>(header.count);
        stats.header_line = header.line;
        stats.declared_vertices = declared;
        stats.legacy_problem_line = header.legacy;
        coordinates.assign(declared, DimacsCoordinate{});
        seen.assign(static_cast<std::size_t>(declared) + 1, std::uint8_t{0});
        have_header = true;
        continue;
      }
      case 'v': {
        if (!have_header) {
          dimacs_detail::fail(
              path, reader.line_number(),
              "coordinate line appears before the problem line");
        }
        dimacs_detail::Tokenizer tokens(trimmed);
        std::string_view token;
        if (!tokens.next(token) || token != "v") {
          dimacs_detail::fail(path, reader.line_number(),
                              "expected 'v <id> <longitude> <latitude>' "
                              "coordinate line");
        }
        if (!tokens.next(token)) {
          dimacs_detail::fail(path, reader.line_number(),
                              "missing vertex id on coordinate line");
        }
        const std::uint64_t id =
            dimacs_detail::to_u64(path, reader.line_number(), token,
                                  "vertex id");
        if (!tokens.next(token)) {
          dimacs_detail::fail(path, reader.line_number(),
                              "missing longitude on coordinate line");
        }
        const std::int64_t x = dimacs_detail::to_i64(
            path, reader.line_number(), token, "longitude");
        if (!tokens.next(token)) {
          dimacs_detail::fail(path, reader.line_number(),
                              "missing latitude on coordinate line");
        }
        const std::int64_t y = dimacs_detail::to_i64(
            path, reader.line_number(), token, "latitude");
        dimacs_detail::require_no_extra_tokens(path, reader.line_number(),
                                               tokens);
        if (id < 1 || id > declared) {
          dimacs_detail::fail(
              path, reader.line_number(),
              "vertex id " + std::to_string(id) + " is outside [1, " +
                  std::to_string(declared) + "]");
        }
        if (x < std::numeric_limits<std::int32_t>::min() ||
            x > std::numeric_limits<std::int32_t>::max() ||
            y < std::numeric_limits<std::int32_t>::min() ||
            y > std::numeric_limits<std::int32_t>::max()) {
          dimacs_detail::fail(
              path, reader.line_number(),
              "coordinate does not fit into int32 (1e-6 degree units)");
        }
        if (seen[id] != 0) {
          dimacs_detail::fail(path, reader.line_number(),
                              "duplicate coordinate for vertex " +
                                  std::to_string(id));
        }
        seen[id] = 1;
        coordinates[id - 1] = {
            static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
        ++stats.coordinates;
        if (!ranges_valid) {
          stats.min_x = stats.max_x = static_cast<std::int32_t>(x);
          stats.min_y = stats.max_y = static_cast<std::int32_t>(y);
          ranges_valid = true;
        } else {
          stats.min_x = std::min(stats.min_x, static_cast<std::int32_t>(x));
          stats.max_x = std::max(stats.max_x, static_cast<std::int32_t>(x));
          stats.min_y = std::min(stats.min_y, static_cast<std::int32_t>(y));
          stats.max_y = std::max(stats.max_y, static_cast<std::int32_t>(y));
        }
        continue;
      }
      default:
        dimacs_detail::fail(
            path, reader.line_number(),
            "unexpected line (expected a 'c', 'p' or 'v' line)");
      }
    }
  }
  if (!have_header) {
    dimacs_detail::fail(path, 0,
                        "missing 'p aux sp co <vertices>' problem line");
  }
  if (options.require_declared_counts &&
      stats.coordinates != stats.declared_vertices) {
    dimacs_detail::fail(path, 0,
                        "problem line declares " +
                            std::to_string(stats.declared_vertices) +
                            " vertices but the file contains " +
                            std::to_string(stats.coordinates) +
                            " coordinate records");
  }
  stats.ranges_valid = ranges_valid;
  if (info != nullptr) {
    *info = std::move(stats);
  }
  return coordinates;
}

} // namespace dijkstra_bench

#endif
