# DIMACS 美国路网数据集解析器

ZJUADS Project 2 要求用 **9th DIMACS Implementation Challenge（Shortest Paths）**
的美国路网数据来评测 Dijkstra 算法。本文档说明：

- 数据文件的格式与获取方式（官方站点已失效，实际来源见 [DATA.md](DATA.md)）；
- 解析器的设计、接口和用法；
- 构建/测试方法，以及后续把解析结果接到 Dijkstra 基准程序的方式。

相关文件：

| 文件 | 作用 |
| --- | --- |
| `pq-test/dimacs-parser.hpp` | 解析器（header-only）：`.gr` / `.co` / `.ss` / `.p2p` → `dijkstra_bench::Graph` |
| `pq-test/csr-graph.hpp` | CSR 图容器 `dijkstra_bench::Graph` |
| `pq-test/dimacs-inspect.cpp` | 命令行工具，解析 + 校验 + 打印统计信息 |
| `pq-test/dimacs-parser-test.cpp` | GoogleTest 单元测试 + 真实数据集成测试 |
| `data/dimacs/` | 下载并解压后的数据（不纳入 git，见 `docs/DATA.md`） |

---

## 1. DIMACS 文件格式

挑战赛的路网数据是纯文本文件（下载得到的是 `.gz` 压缩包，先解压再解析），
节点编号从 **1** 开始，权重是非负 32 位整数。共四类文件：图 `.gr`、坐标 `.co`、
单源查询 `.ss`、点对点查询 `.p2p`。

### 1.1 图文件 `.gr`

```text
c 9th DIMACS Implementation Challenge: Shortest Paths
c TIGER/Line graph USA-road-d.NY
p sp 264346 733846
a 1 2 2008
a 2 1 2008
a 3 4 395
```

- `c ...`：注释行，数量任意，可出现在任何位置（解析器全部忽略）；
- `p sp <n> <m>`：问题行，**必须恰好出现一次**，`n` 为顶点数，`m` 为弧数；
- `a <u> <v> <w>`：有向弧 `u -> v`，权重 `w >= 0`。

路网是有向图，无向道路在文件里通常成对出现（`a u v w` 和 `a v u w`）。
`USA-road-d.*` 的权重是**距离**，`USA-road-t.*` 的权重是**旅行时间**。

### 1.2 坐标文件 `.co`

```text
p aux sp co 264346
v 1 -73530767 41085396
v 2 -73530538 41086098
```

- `p aux sp co <n>`：声明 `n` 个顶点；
- `v <id> <longitude> <latitude>`：坐标，单位是 **1e-6 度**（WGS 84），
  上例 `-73530767` 表示经度 -73.530767°。

> 注：官方基准包里自带的样例文件 `docs/format/samples/sample.co.txt`（以及
> `data/dimacs/samples/sample.co`）用的是**旧式头 `p sp co <n>`**，与格式说明页写的
> `p aux sp co <n>` 不一致。解析器两种都接受；真实路网 `.co` 文件用的是
> `p aux sp co <n>`。

### 1.3 查询文件：`.ss`（单源）与 `.p2p`（点对点）

挑战赛定义了两种查询文件（见官方 `docs/format/format.shtml`）：

**单源（single-source，`.ss`）**——每行一个源点：

```text
p aux sp ss 3
s 1
s 3
s 5
```

**点对点（point-to-point，`.p2p`）**——每行一组源-汇点对，**Project 2 要求的
"1000 pairs of query" 使用这种格式**：

```text
p aux sp p2p 1000
q 12 3456
q 7 260101
```

- 两者都以 `p aux sp <ss|p2p> <n>` 声明记录数；
- `.ss` 的行前缀是 `s`，只跟一个顶点；`.p2p` 的行前缀是 `q`，跟 `源 汇` 两个顶点。

> 注：官方数据包的 `samples/` 下有 `sample.ss`、`sample.p2p` 等样例文件
> （本地位于 `data/dimacs/samples/`，由下载脚本一并获取），可以直接用来做
> 格式对照测试。官方并未随 USA 路网发布现成的点对点查询文件，Project 2 需要的
> 1000 组查询由 `pq-test/tools/gen-p2p-queries.py` 按官方参数（1000 对、seed 971）
> 生成，放在 `data/dimacs/queries/`，详见 [DATA.md](DATA.md)。

---

## 2. 解析器设计

### 2.1 数据结构：CSR

解析结果存放在 `dijkstra_bench::Graph`（`pq-test/csr-graph.hpp`）中，采用
**CSR（compressed sparse row）** 邻接表：

- 顶点编号 **1-based**（`1 .. num_vertices()`），与 DIMACS 文件和旧版基准代码
  一致，后续 Dijkstra 不需要做下标平移；
- `offsets()` 有 `n + 1` 项，`offsets()[u]` 是顶点 `u` 的弧在 `edges()` 中
  的结束位置，因此顶点 `u` 的弧区间是 `[offsets()[u-1], offsets()[u])`；
- `operator[](u)` 返回顶点 `u` 的出边 `std::span<const Edge>`，`Edge { v, w }`，
  顺序与文件中的出现顺序一致；
- 总弧数 `num_edges()`、顶点数 `num_vertices()`。

### 2.2 两遍扫描

`.gr` 文件可能很大（USA 全图 23,947,347 节点、58,333,344 弧，见官方下载页），
解析器**不把文件整体读入内存**，
而是用自定义的 1 MiB 缓冲阅读器扫描两遍：

1. **第一遍**：校验语法；读 `p` 行得到 `n/m`；统计每个顶点的出度；同时收集
   权重 min/max/sum、自环数、最大节点编号等统计量；
2. 对出度做前缀和，直接得到 CSR 的 offset 数组；
3. **第二遍**：重新扫描文件，把每条弧填进 `edges()` 中它所属的位置。

因此峰值内存约为

```text
m * sizeof(Graph::Edge)（边数组）+ (n+1) * (4+4) 字节（offset/cursor）
+ (n+1) 字节（--check 时的 touched 数组）
```

当前 `Graph::Edge` 是 `{ uint32_t v; int64_t w; }`（16 字节），所以约

```text
16m + 8n 字节（加 --check 时 +n 字节）
```

例如 NY（n=264346，m=733846）约 14 MB；E（n=3598623，m=8778114）约 169 MB。
（若以后调整 `Graph::Edge` 的字段，系数按 `sizeof` 相应变化。）

### 2.3 校验项

解析失败会抛出 `dijkstra_bench::DimacsParseError`：行级错误的信息形如
`<file>:<line>: <problem>`（例如 `USA-road-d.NY.gr:59435: ...`），文件级错误
（缺少/重复问题行、声明数量与实际不符等）形如 `<file>: <problem>`。校验包括：

- 问题行缺失/重复、问题类型不是 `sp`、顶点数为 0、顶点数/弧数超出 32 位
  CSR 范围（顶点数上限为 2^32-2）；
- 弧行字段缺失/多余、顶点编号不在 `[1, n]`、权重为负或超过 `int32`；
- 权重总和溢出 `uint64`；
- 声明弧数/记录数与实际行数不一致（可用 `DimacsParseOptions::
  require_declared_counts = false` 放宽）；
- `.co`：顶点 id 越界、重复、记录数不符；查询文件（`.ss`/`.p2p`）：id 越界、
  查询数不符、用错格式（例如 `.p2p` 文件里出现单源 `s` 行）会报错；
- 可选：`validate_vertex_coverage` 统计从未出现在任何弧中的顶点，
  `require_vertex_coverage` 直接报错。

`dimacs-inspect` 还会做跨文件一致性校验：`--coords` 声明的顶点数必须等于
`--queries` 所属图（`.gr`）的顶点数，`.p2p`/`.ss` 的顶点 id 必须落在 `[1, n]` 内。

### 2.4 输入容错

解析器接受以下"不标准但常见"的情况：

- CRLF（Windows 换行）、文件末尾没有换行符；
- 空行、任意位置的行首空格/Tab、多空格/Tab 分隔的字段；
- UTF-8 BOM（部分镜像会重新编码文件）；
- 单行超过 1 MiB（缓冲阅读器自动拼接跨缓冲区的行）。 

> 解析器只读取**未压缩**文本，请先 `gzip -d`/解压（下载脚本会自动解压）。

---

## 3. 接口

```cpp
#include "./dimacs-parser.hpp"

namespace db = dijkstra_bench;

// 解析 .gr
db::DimacsParseOptions options;          // 默认严格校验
options.validate_vertex_coverage = true; // 可选：统计孤立点
db::DimacsGraphInfo info;                // 可选：输出统计信息
db::Graph graph = db::read_dimacs_gr("data/dimacs/USA-road-d.NY.gr",
                                     options, &info);

// 顶点 1 .. graph.num_vertices()，出边顺序 = 文件顺序
for (std::uint32_t u = 1; u <= graph.num_vertices(); ++u) {
  for (const db::Graph::Edge &e : graph[u]) {
    // e.v: 弧头（1-based），e.w: 权重
  }
}

// 解析 .ss（单源）；第二个参数用于校验 id 范围，传 0 表示不校验
std::vector<std::uint32_t> sources =
    db::read_dimacs_ss("data/dimacs/samples/sample.ss", graph.num_vertices());

// 解析 .p2p（点对点，项目要求的 1000 组查询）
std::vector<db::DimacsQuery> pairs =
    db::read_dimacs_p2p("data/dimacs/queries/USA-road-d.NY.p2p", graph.num_vertices());
// pairs[i].source / pairs[i].target

// 或不区分类型：自动根据 p aux sp ss/p2p 判断
db::DimacsQueryFile query_file =
    db::read_dimacs_queries("data/dimacs/queries/USA-road-d.NY.p2p", graph.num_vertices());

// 解析 .co；返回值第 id-1 项对应顶点 id
std::vector<db::DimacsCoordinate> coords =
    db::read_dimacs_co("data/dimacs/USA-road-d.NY.co");
```

`DimacsGraphInfo` 提供 `declared_vertices`、`declared_arcs`、`arcs`、
`max_vertex_id`、`self_loops`、`min/max_weight`、`weight_sum`、
`min/max_out_degree`、`isolated_vertices` 等统计量。

---

## 4. 构建、运行与测试

本仓库用 CMake + Ninja + MSYS2 UCRT64 的 g++ 构建。**注意**：本机的
`g++.exe` 在 PowerShell 里如果找不到 `C:\msys64\ucrt64\bin` 下的运行时 DLL，
会出现"静默失败"（退出码 1、无任何报错）。构建/运行前先把该目录加进 PATH，
或直接在 MSYS2 UCRT64 终端里操作：

```powershell
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cd ads-projects\proj2
cmake --build build -j
```

解析器对应的两个构建目标：

- `build/pq-test/dimacs-inspect.exe`：命令行工具；
- `build/pq-test/dimacs-parser-test.exe`：单元测试。

```powershell
# 解析 + 校验真实数据（--check 会额外检查所有顶点是否都出现在弧中）
.\build\pq-test\dimacs-inspect.exe data\dimacs\USA-road-d.NY.gr `
    --coords data\dimacs\USA-road-d.NY.co --check

# 机器可读输出
.\build\pq-test\dimacs-inspect.exe --json data\dimacs\USA-road-d.NY.gr

# 单元测试（数据没下载时真实数据用例会自动 skip）
ctest --test-dir build --output-on-failure
```

`dimacs-inspect` 输出示例（真实 NY 数据，2026-10-26 实测）：

```text
DIMACS graph: data\dimacs\USA-road-d.NY.gr
  problem line:              line 5 (p sp 264346 733846)
  vertices:                  264346 (declared 264346)
  arcs:                      733846 (declared 733846)
  max vertex id:             264346
  self-loops:                0
  arc weight min/max/sum:    1 / 36946 / 949085490
  out-degree min/avg/max:    1 / 2.77608 / 8
  vertices without arcs:     0
  parse time:                0.062 s (11.93 M/s arcs)

DIMACS coordinates: data\dimacs\USA-road-d.NY.co
  problem line:              line 5 (p aux sp co 264346)
  records:                   264346 (declared 264346)
  longitude range (deg):     -74.499998 .. -73.500016
  latitude  range (deg):     40.300009 .. 41.299997
  parse time:                0.013 s
```

在已下载的数据集上实测（Release，MSYS2 UCRT64 g++ 16.2，2026-10-26）：

| 数据集 | 顶点 n | 弧 m | 解析时间 | 峰值内存（近似） |
| --- | --- | --- | --- | --- |
| `USA-road-d.NY` | 264 346 | 733 846 | 0.06 s | ~14 MB |
| `USA-road-d.BAY` | 321 270 | 800 172 | 0.06 s | ~15 MB |
| `USA-road-d.NE` | 1 524 453 | 3 897 636 | 0.35 s | ~75 MB |
| `USA-road-d.E` | 3 598 623 | 8 778 114 | 0.82 s | ~169 MB |
| `USA-road-t.NY` | 264 346 | 733 846 | 0.06 s | ~14 MB |

（解析是两遍扫描，表中时间为整个 `read_dimacs_gr`，吞吐约 11–12 M arcs/s。）

选项：

| 选项 | 说明 |
| --- | --- |
| `--coords <file.co>` | 额外解析/校验坐标文件 |
| `--queries <file>` | 额外解析/校验查询文件（`.p2p` 点对点或 `.ss` 单源，自动识别） |
| `--no-strict` | 不要求声明数量与实际行数一致 |
| `--check` | 统计"从未出现在任何弧中"的顶点 |
| `--json` | 输出 JSON 汇总（便于脚本/报告处理） |

退出码：`0` 成功，`1` 解析失败，`2` 命令行参数错误。

---

## 5. 测试覆盖

`dimacs-parser-test.cpp` 共 31 个 GoogleTest 用例，覆盖：

- `.gr`：基本图 → CSR 结构/顺序；CRLF、空行、Tab、无末尾换行；UTF-8 BOM；
  超过读缓冲的超长行；空弧集；顶点覆盖统计；缺问题行/重复问题行；
  声明弧数不符（严格/放宽）；未知行且报错行号；顶点越界；负权重/int32 溢出；
  弧行字段缺失/多余/非数字；问题类型错误；1000 顶点 / 5000 弧一致性；
- `.ss`（单源）：解析与范围校验、单源格式下出现两个 id 报错、不传顶点数时
  不校验、声明数量不符；
- `.p2p`（点对点）：解析与范围校验、声明数量不符；
- 查询格式自动识别：`read_dimacs_queries` 正确区分 ss/p2p，用错 wrapper 时报错；
- `.co`：解析与坐标范围、id 越界、重复 id、记录数不符、问题类型错误、
  旧式 `p sp co n` 问题行兼容；
- 真实数据（`data/dimacs/USA-road-d.NY.gr/.co`，数据未下载时自动 skip）：
  264346 顶点 / 733846 弧、权重 1..36946、无孤立点、坐标范围；
- 官方样例文件（`data/dimacs/samples/sample.ss` / `sample.p2p` / `sample.gr` /
  `sample.co`，未下载时自动 skip）：可作为真实格式的对照。

---

## 6. 参考链接

- 9th DIMACS Implementation Challenge（官方站点，目前无法访问）：
  <http://www.diag.uniroma1.it/challenge9/download.shtml>
- 数据集的实际镜像与校验信息：[DATA.md](DATA.md)
- 项目要求：[PROJECT.md](PROJECT.md)
