# DIMACS 第 9 届实现挑战赛 —— USA 公路网数据集（proj2）

本文档记录 `ads-projects/proj2` 使用的官方 **9th DIMACS Implementation Challenge: Shortest
Paths** USA 公路网数据的来源、获取方式、校验值、格式说明、可复现步骤，以及
`.ss` / `.p2p` 查询文件的调查结论。

- 采集日期：**2026-10-26**（P0/P1/P2 六文件，`data-hunter`）；**2026-10-28**（补齐完整 48 文件集，`lead`）
- 数据根目录：`data/dimacs/`（已加入 `proj2/.gitignore`，**不提交**；用脚本可完整重建）
- 官方站点：<http://www.diag.uniroma1.it/challenge9/> —— **已下线**（HTTP 502 / TLS 握手失败，
  2026-10-26 实测），因此全部数据来自 Internet Archive Wayback Machine 归档与 GitHub 镜像。

---

## 1. 数据集概述

第 9 届 DIMACS 实现挑战赛（2005–2006，Camil Demetrescu / Andrew Goldberg / David S. Johnson）
提供了美国公路网图，用于最短路算法基准测试：

| 家族 | 含义 | 文件名模式 |
| --- | --- | --- |
| `USA-road-d` | 边权 = 道路**距离**（TIGER/Line 派生） | `USA-road-d.<REGION>.gr.gz` |
| `USA-road-t` | 边权 = 通行**时间** | `USA-road-t.<REGION>.gr.gz` |
| 坐标 | 节点经/纬度 | `USA-road-d.<REGION>.co.gz` |

`<REGION>` 取值：`NY BAY COL FLA NE NW CAL LKS E W CTR USA`。2026-10-26 首轮按需下载了
**NY、BAY、NE、E** 四个距离图 + NY 的坐标 + NY 的通行时间图，构成从小到大的基准阶梯
（26 万 → 360 万节点）；**2026-10-28 已补齐为全部 12 个区域 × d/t 两个家族 × .gr/.co 的
48 个文件，完整清单、来源与校验见 §9**。首轮六文件实测体量（2026-10-26）：

- 总下载量（`raw/` 原始 `.gz`）：**87,090,938 B（≈87.1 MB / 83.1 MiB）**
- 解压后纯文本：**327,603,114 B（≈327.6 MB / 312.4 MiB）**，其中
  E 193.0 MB、NE 82.2 MB、BAY 15.9 MB、NY.gr 14.4 MB、NY.co 7.3 MB、NY-t 14.8 MB（十进制）；
  五个 `.gr` 合计约 305 MiB（不含 NY.co 的 7.0 MiB）
- `data/dimacs/` 数据文件本体（完整 48 文件）：**12,640,688,380 B（≈12.64 GB / 11.77 GiB）**
  （原始 `.gz` 2,784,113,022 B + 解压明文 9,856,575,358 B；另含 `samples/`、`queries/`、
  `reference/`、`verification/` 与 `MANIFEST.sha256` 约 0.85 MB）


数据体量（本次实测，均经三重校验，见 §4）：

| 图 | 节点 n | 弧 m | 解压后大小 | 用途 |
| --- | ---: | ---: | ---: | --- |
| `USA-road-d.NY.gr` | 264,346 | 733,846 | 13.8 MiB | P0：开发/正确性基准 |
| `USA-road-d.BAY.gr` | 321,270 | 800,172 | 15.1 MiB | P1：基准阶梯 |
| `USA-road-d.NE.gr` | 1,524,453 | 3,897,636 | 78.4 MiB | P1：基准阶梯 |
| `USA-road-d.E.gr` | 3,598,623 | 8,778,114 | 184.1 MiB | P1：基准阶梯 |
| `USA-road-t.NY.gr` | 264,346 | 733,846 | 14.1 MiB | P2：可选，通行时间权重 |

> 说明：官方 d 图是**有向图表示**（每条无向道路存两条弧，因此 m ≈ 2×无向边数），
> 弧权为整数、无负权、无自环（C++ 解析器实测 self-loops = 0）。

---

## 2. 官方来源、镜像与获取路径

### 2.1 官方 URL（当前不可用，仅作来源记录）

```
http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.<REGION>.gr.gz
http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.<REGION>.co.gz
http://www.diag.uniroma1.it/challenge9/data/USA-road-t/USA-road-t.<REGION>.gr.gz
http://www.diag.uniroma1.it/challenge9/samples/sample.<ss|p2p|gr|co|...>
http://www.diag.uniroma1.it/challenge9/download.shtml     （官方下载页）
http://www.diag.uniroma1.it/challenge9/format.shtml       （官方格式说明页）
```

站点现状（2026-10-26 实测）：

- `http://www.diag.uniroma1.it/challenge9/download.shtml` → **HTTP 502**
- `https://www.diag.uniroma1.it/challenge9/download.shtml` → TLS 握手失败

### 2.2 Wayback Machine 取档方式

使用 Wayback 的**原始字节**接口 `id_`（不做 HTML 重写/重压缩）：

```
https://web.archive.org/web/<14 位时间戳>id_/<官方 URL>
```

例：

```
https://web.archive.org/web/20230413134046id_/http://www.diag.uniroma1.it/challenge9/data/USA-road-d/USA-road-d.NY.gr.gz
```

CDX 查询接口（用于枚举归档、确认只有哪些文件被存档）：

```
https://web.archive.org/cdx/search/cdx?url=diag.uniroma1.it/challenge9/*&output=json&fl=timestamp,original,statuscode,length&collapse=urlkey&limit=5000
```

> ⚠️ **CDX 的 `length` 字段是 WARC 记录长度，不等于 HTTP payload 大小**。
> 例如 NY.gr：CDX length = 3,657,144，而实际 payload = 3,667,907
> （回放响应头 `x-archive-orig-content-length` 也是 3,667,907）；BAY.gr CDX = 4,131,896，
> payload = 4,130,672。因此本项目一律以**实际下载字节数 + SHA-256** 为准，而不是 CDX length。
> 归档侧偶发 503/429/504，下载脚本已内置 `--retry 6 --retry-delay 5 --retry-all-errors`。

### 2.3 本次实际使用的来源

| 文件 | 来源 | Wayback 时间戳 / 直链 | 官方 URL 是否归档 |
| --- | --- | --- | --- |
| `USA-road-d.NY.gr.gz` | Wayback（主）/ GitHub 镜像（备） | `20230413134046` | ✅ |
| `USA-road-d.NY.co.gz` | **GitHub 镜像** | — | ❌ **官方 .co 从未被归档** |
| `USA-road-d.BAY.gr.gz` | Wayback | `20240122163700` | ✅ |
| `USA-road-d.NE.gr.gz` | Wayback | `20240123141930` | ✅ |
| `USA-road-d.E.gr.gz` | Wayback | `20231202184547` | ✅ |
| `USA-road-t.NY.gr.gz` | Wayback（主）/ GitHub 镜像（备） | `20240123141344` | ✅ |
| 官方示例文件 `samples/*` | Wayback（新版站点 `diag.uniroma1.it`） | 见 §3.3 | ✅ |
| 官方基准包 `ch9-1.1.tar.gz` | Wayback 旧版站点（`dis.uniroma1.it`） | `20110606172252` | ✅ |

GitHub 镜像：<https://github.com/XiangQibu/NEWYORK_DATASET>

- 该仓库同时提供 `USA-road-d.NY.gr.gz`、`USA-road-d.NY.co.gz`、`USA-road-t.NY.gr.gz`。
- **交叉验证**：仓库里的 `USA-road-d.NY.gr.gz` 与 Wayback 官方归档 **SHA-256 完全一致**
  （`7b2446c7…56d4`，3,667,907 B）；`USA-road-t.NY.gr.gz` 也与官方归档一致
  （`de902573…7156`，3,767,743 B）。因此用同一仓库的 `.co` 填补官方未归档的 `NY.co` 是可信的；
  但仍如实标注：**`USA-road-d.NY.co.gz` 属于「镜像来源 + 结构校验」，不是官方归档原件**
  （官方原件已随站点下线且 Wayback 未捕获，无法做逐字节比对）。校验见 §3.2。

---

## 3. 已获取文件清单与校验值

校验方法：`Get-FileHash -Algorithm SHA256`（PowerShell）/ `sha256sum` 等价的 SHA-256。
所有 `.gz` 均完整解压（gzip CRC 由 .NET `GZipStream` 在流结束时校验，无错误）。
`data/dimacs/MANIFEST.sha256` 汇总了下列全部文件（路径相对 `data/dimacs/`）。

### 3.1 主数据（raw + 解压后）

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `raw/USA-road-d.NY.gr.gz` | 3,667,907 | `7b2446c7ffe6179efbc42af6812448e8cb782f12968ca2bb1d50d979343056d4` |
| `USA-road-d.NY.gr` | 14,437,459 | `37f5b3eff82330850466956b6c036a8ab8551512bdc7509edca1d4c6cf943fa3` |
| `raw/USA-road-d.NY.co.gz` | 2,058,598 | `02547a628742164e02bd73f59e33a65d23dfac8fc54c09275c8b9169c38eafdf` |
| `USA-road-d.NY.co` | 7,290,781 | `c39ec9b87c66d6e254f80cd3cfe20c7eb3c5ce4aca4063dc2efc228ab12fa95f` |
| `raw/USA-road-d.BAY.gr.gz` | 4,130,672 | `630c4a96869b3ecdd631ceb0f63923fb954ee4c7c26d983863612e2c4442462e` |
| `USA-road-d.BAY.gr` | 15,851,432 | `75a1a78d1f1bd1d8d86b2dc81b0c2ac30f701e078c514c9f9b953130b83f979e` |
| `raw/USA-road-d.NE.gr.gz` | 21,755,467 | `edd4f51db01345f44d8972a8c372b6baaa5b2026182dd95668572035db55471e` |
| `USA-road-d.NE.gr` | 82,240,991 | `24985e9b4d7feeb00e2e52394627bb00ea52e7f85b2f7640aa6139fd8adc0394` |
| `raw/USA-road-d.E.gr.gz` | 51,710,551 | `d2e59a82ec5892491d4bfe1e30b0fd3344ae5864e783eb225a843bc5368fe17d` |
| `USA-road-d.E.gr` | 193,023,156 | `80a6b825405501b65878d84c92a0ce54c27ec0e91f6c6724e28663fb114f0509` |
| `raw/USA-road-t.NY.gr.gz` | 3,767,743 | `de902573142dbf12c42b065d9a6a679e4794372eb91d2748ee78089720377156` |
| `USA-road-t.NY.gr` | 14,759,295 | `948377c7f0a974bdb0a9be76fd5b7ceff95bb78c2a011fa6661bde2f2f06f732` |

对应头部行与内容统计（**由两个独立实现交叉验证**：`pq-test/tools/verify-dimacs.py`（Python）
与 Lead 的 C++ `build/pq-test/dimacs-inspect.exe`，数值一致）：

| 文件 | 头部 problem line | n | m | 弧权 min/max | 弧权总和 | 最大出度/最小出度 | 无弧节点 |
| --- | --- | ---: | ---: | --- | ---: | --- | ---: |
| `USA-road-d.NY.gr` | `p sp 264346 733846` | 264,346 | 733,846 | 1 / 36,946 | 949,085,490 | 8 / 1（avg 2.77608） | 0 |
| `USA-road-d.BAY.gr` | `p sp 321270 800172` | 321,270 | 800,172 | 1 / 94,305 | 1,304,414,916 | 7 / 1（avg 2.49065） | 0 |
| `USA-road-d.NE.gr` | `p sp 1524453 3897636` | 1,524,453 | 3,897,636 | 1 / 63,247 | 6,684,192,250 | 9 / 1（avg 2.55674） | 0 |
| `USA-road-d.E.gr` | `p sp 3598623 8778114` | 3,598,623 | 8,778,114 | 1 / 200,760 | 18,571,138,508 | 9 / 1（avg 2.43930） | 0 |
| `USA-road-t.NY.gr` | `p sp 264346 733846` | 264,346 | 733,846 | 2 / 92,366 | 2,294,518,348 | 8 / 1（avg 2.77608） | 0 |

验证要点（每个 `.gr` 全部通过）：

- `p sp <n> <m>` 存在且位于所有数据行之前；
- `a` 行数量 **精确等于** m；
- 所有节点 id ∈ [1, n]，最大 id == n，且 1..n **每个节点都出现在弧中**（missing = 0）；
- 无格式错误行；gzip 完整解压无 CRC 错误。

### 3.2 坐标文件 `USA-road-d.NY.co`

| 项目 | 值 |
| --- | --- |
| 头部 | `p aux sp co 264346` |
| 记录数 | 264,346（与 n 一致，无重复、无缺失） |
| 节点 id | [1, 264346]，最大 id = 264346 |
| 经度范围（原始整数，单位 1e-6 度） | −74,499,998 .. −73,500,016（= −74.499998° .. −73.500016°） |
| 纬度范围（原始整数，单位 1e-6 度） | 40,300,009 .. 41,299,997（= 40.300009° .. 41.299997°） |
| 格式 | `v <id> <lon> <lat>`，坐标为整数（×1e6 度），lon 在前、lat 在后 |

> `USA-road-d.NY.co.gz` 的**来源注意**：官方 `NY.co.gz` 从未被 Wayback 捕获（CDX 只有
> CAL/CTR/FLA/NE/NW/W 的 `.co`），故取自 GitHub 镜像 `XiangQibu/NEWYORK_DATASET`。
> 可信度依据：(a) 同仓库 `NY.gr.gz` 与官方 Wayback 归档 SHA-256 逐字节一致；
> (b) `.co` 自身结构完整（n 匹配、id 连续覆盖、坐标落在纽约州经纬度范围内）；
> (c) C++ 解析器 `--coords` 校验通过。**它无法被证明与已失传的官方原件逐字节相同**——
> 若日后出现官方 `.co` 归档，应优先替换。

### 3.3 官方示例文件（仅用于格式/解析器测试，共 13 个文件，< 9 KiB）

`data/dimacs/samples/` 中来自 Wayback 的官方站点原文件（9 个）：

| 文件 | 字节 | SHA-256 |
| --- | ---: | --- |
| `sample.gr` | 305 | `2a0dc45b7dfed3ff26d7d1a084529d4eafed422baf5d0381d22741e010d193b2` |
| `sample.co` | 279 | `13c0eff2673b55eee272fdb97d0f811b7411b9bae5b659a4ce043f860064fc54` |
| `sample.ss` | 201 | `e5551105c6cd2376a7157daecb19f7ef58bf9933ba7fe7c249c5ff58b02db9fa` |
| `sample.ss.chk` | 923 | `9e1ecc5db2d8943fb95788844391da634b4ccb6c7a01014711c07aaebce0fa01` |
| `sample.ss.res` | 1,805 | `a59b4e3acd6a246dc8a71ab54f5115774587bab1a60191a8c22c3ae558d6f0f7` |
| `sample.p2p` | 209 | `67b90c690f429c83e4f97c6e26ffe1084e78e1fcb5d64d400afd954c4b64cac2` |
| `sample.p2p.chk` | 863 | `fd202c5220848889b45950230c3d644d5f35139e09032e592197b2fb35650b61` |
| `sample.p2p.p.res` | 1,322 | `e216497b4287899ab24fa1c197c6f52ef76a083c437cf1be4d2a7ea68af72f39` |
| `sample.p2p.q.res` | 1,895 | `cbed7d88599f230c3e6cb2d2ad6c3ecf008ff91e6e09a540af987663e6c997ff` |

另有 4 个 `*.txt` 是官方基准包 `ch9-1.1` 的 `docs/format/samples/` 副本（文档用途）：
`sample.gr.txt`、`sample.co.txt`、`sample.ss.txt`、`sample.p2p.txt`。

这些是**真正的官方示例**，不是生成文件；`sample.ss` / `sample.p2p` 展示了 §5 两种查询格式。

### 3.4 官方基准包与归档证据（`data/dimacs/reference/`）

| 文件 | 字节 | SHA-256 | 说明 |
| --- | ---: | --- | --- |
| `ch9-1.1.tar.gz` | 379,759 | `c1367ac0d27f752860630ae6a962804828d5784995b987e470831a0da3855858` | 官方基准包 v1.1（旧域名 Wayback `20110606172252`） |
| `format.shtml` | 100,598 | `8e821c0efb2123152c28eea4a8f7bf5e1ffa977e68d4ec5a09cf3426269fff40` | 官网格式说明页归档（Wayback `20170520175037` 捕获） |
| `download.shtml.2017.html` | 20,245 | `b5ecc4ed1d55d95350cef68146e8af8b9f95638a2be5a1f899ec7c1823a66b30` | 官方下载页归档（2017）：**只列 `.gr.gz`/`.co.gz`，无任何 `.ss`/`.p2p` 下载项** |
| `ch9-1.1/` | — | — | 从 tar.gz 中只提取的**官方格式文档 + 查询文件生成器 + local_q 源码**（见下） |
| `evidence/cdx-challenge9-all.json` | 28,550 | — | CDX 全站 URL 清单（291 条，2026-10-26） |
| `evidence/cdx-usa-road-d.json` | 1,949 | — | CDX `data/USA-road-d/` 清单 |
| `evidence/cdx-dis-old.json` | 28,552 | — | 旧域名 `dis.uniroma1.it/~challenge9` CDX 清单（309 条，含 2006 年样例/代码） |
| `evidence/gh-newyork-dataset-contents.json` | 29,167 | — | GitHub 镜像仓库文件清单 |

`reference/ch9-1.1/` 保留内容（官方原件，未改动）：

```
ch9-1.1/README.txt                     官方基准包说明（含如何生成 ss/p2p 实例）
ch9-1.1/inputs-README.txt              输入文件命名约定（.gr/.ss/.p2p/.base-rank.p2p）
ch9-1.1/docs/format/format.html        官方格式说明（.gr .co .ss .p2p 权威定义）
ch9-1.1/docs/format/samples/*.txt      官方示例文件（文档副本）
ch9-1.1/scripts/genUSA-road-d.rnd.p2p.pl   官方随机 p2p 查询生成器（NUMQUERY=1000, SEED=971）
ch9-1.1/scripts/genUSA-road-d.loc.p2p.pl   官方局部(local) p2p 查询生成器（100 源 × Dijkstra rank）
ch9-1.1/scripts/genUSA-road-d.ss.pl        官方单源(ss)查询生成器（nsrc = int(10*n_CTR/n)）
ch9-1.1/scripts/genUSA-road-d.gr.pl        官方图下载脚本
ch9-1.1/scripts/runUSA-road-d.*.pl         官方运行/基准脚本
ch9-1.1/scripts/README.txt                 脚本说明 + 图的生成流水线（cutUSA-road.pl 等）
ch9-1.1/gens/local_q/                      官方 local p2p 生成器 C++ 源码 + DOC
ch9-1.1/gens/utils/genSources.pl           官方 ss 源点生成器
```

---

## 4. 校验方法（可复现）

### 4.1 一键重建：`pq-test/tools/fetch-dimacs.ps1`

要求 PowerShell 7+、`curl.exe`（Windows 10/11 自带）；可选 `python`（本脚本不依赖）。
**只写入 `data/dimacs/`，不触碰任何源码/CMake。**

```powershell
# 从 proj2 根目录（或任意位置，脚本会自动定位 proj2 根）：
pwsh pq-test/tools/fetch-dimacs.ps1                    # 默认 P0+P1+P2（6 个文件，~87 MB）
pwsh pq-test/tools/fetch-dimacs.ps1 -Set P0            # 仅 NY.gr + NY.co（最小可用）
pwsh pq-test/tools/fetch-dimacs.ps1 -IncludeSamples    # 默认数据 + 官方示例文件
pwsh pq-test/tools/fetch-dimacs.ps1 -Set Samples       # 仅 9 个官方示例文件
pwsh pq-test/tools/fetch-dimacs.ps1 -Files NY.gr,NY.co # 指定文件（id 见脚本内嵌表）
pwsh pq-test/tools/fetch-dimacs.ps1 -Offline           # 只校验本地已有文件，不联网
pwsh pq-test/tools/fetch-dimacs.ps1 -Force             # 强制重新下载+解压
```

脚本对每个文件执行 5 个步骤：

1. 按内嵌表依次尝试候选 URL（Wayback 优先，GitHub 镜像兜底），
   只有**字节数 + SHA-256 均与内嵌期望值一致**才落到目标路径（先写 `.part`，避免半截文件）；
2. 校验 `.gz` 的字节数与 SHA-256；
3. 用 .NET `GZipStream` 完整解压（流结束自动校验 gzip CRC），写 `data/dimacs/<无 .gz 名>`，
   并校验解压后的字节数与 SHA-256；
4. 逐行解析 DIMACS 文本并核对头部计数：
   `.gr` 的 `a` 行数 == m、节点 id ∈ [1,n]、max id == n、min/max 弧权、缺失节点统计；
   `.co` 的 `v` 行数 == n、id ∈ [1,n]、max id == n、重复 id 检查；
5. 重写 `data/dimacs/MANIFEST.sha256`。

已有文件 hash 正确时会跳过下载/解压（幂等）。因为 SHA-256 已是强校验，第 4 步属于
“可读性 + 计数”的二次确认。注意：PowerShell 逐行解析较慢，尤其是 `E` 图（878 万条弧），
整套 6 个文件 `-Offline` 离线复验实测约 **8–10 分钟**（主要是 E 的逐行解析）；日常复验可直接用
§4.2 的 SHA-256 清单（秒级）。

### 4.2 MANIFEST.sha256 与 sha256sum 校验

`data/dimacs/MANIFEST.sha256` 为 sha256sum 兼容格式（`<hash><两空格><相对路径>`），
路径相对 `data/dimacs/`，含 raw `.gz`、解压文件、`samples/`、`queries/` 全部文件：

```bash
cd data/dimacs && sha256sum -c MANIFEST.sha256
```

PowerShell 等价：

```powershell
Get-Content data/dimacs/MANIFEST.sha256 | ForEach-Object {
  $h, $p = $_ -split '  ', 2
  if ((Get-FileHash ("data/dimacs/" + $p) -Algorithm SHA256).Hash.ToLower() -ne $h) { "FAIL $p" } else { "OK   $p" }
}
```

### 4.3 Python 逐文件验证器：`pq-test/tools/verify-dimacs.py`

```powershell
# 直接校验 .gz（同时输出 JSON 报告），并用 --extract-to 顺带解压
python pq-test/tools/verify-dimacs.py data/dimacs/raw/USA-road-d.NY.gr.gz --extract-to data/dimacs/USA-road-d.NY.gr
python pq-test/tools/verify-dimacs.py data/dimacs/USA-road-d.NY.co.gz --extract-to data/dimacs/USA-road-d.NY.co
```

输出包含 `n/m/arc_lines/min-max weight/max node id/missing nodes/错误列表`，退出码 0=通过、1=失败。
（本次采集过程中已对全部文件运行，报告存档于 `data/dimacs/verification/*.report.json`。）

### 4.4 C++ 解析器交叉验证（Lead 的实现）

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
.\build\pq-test\dimacs-inspect.exe data\dimacs\USA-road-d.NY.gr --coords data\dimacs\USA-road-d.NY.co --queries data\dimacs\queries\USA-road-d.NY.p2p --check
```

实测（2026-10-26，与 Python 校验结果一致）：NY 图 `vertices 264346 / arcs 733846 /
max vertex id 264346 / self-loops 0 / weight min-max-sum 1-36946-949085490 /
out-degree min-avg-max 1-2.77608-8 / vertices without arcs 0`，坐标与 p2p 查询文件同样通过。
各图完整输出存档于 `data/dimacs/verification/`。

---

## 5. 查询文件格式：`.ss`（单源）与 `.p2p`（点对点）

两种格式的权威定义见 `data/dimacs/reference/format.shtml` 与官方包
`reference/ch9-1.1/docs/format/format.html`：

### 5.1 `.ss` —— 单源最短路（single-source）

```text
c 9th DIMACS Implementation Challenge: Shortest Paths
p aux sp ss 3
s 1
s 3
s 5
```

- 头部：`p aux sp ss <count>`，`count` = 源点行数；
- 源点行：`s <source>`（**每行只有一个节点 id**）；
- 语义：对同一张图、每个源点分别跑一次单源最短路。
- 官方示例：`data/dimacs/samples/sample.ss`（3 个源点）。

### 5.2 `.p2p` —— 点对点（point-to-point，本项目 “1000 组查询对” 用的格式）

```text
c Sample point-to-point aux file
p aux sp p2p 3
q 1 5
q 5 1
q 1 2
```

- 头部：`p aux sp p2p <count>`，`count` = 查询对数量；
- 查询行：`q <source> <target>`（**每行一对**）；
- 语义：同一张图上回答 `count` 组 (s,t) 最短路。
- 官方示例：`data/dimacs/samples/sample.p2p`（3 对）。

> **结论：`.ss` 是单源格式，不等于“查询对”。** 本项目 1000 组 source-target 对使用
> `.p2p` 格式；Lead 的 C++ 解析器已同时支持两种格式（`--queries` 自动判别）。

---

## 6. 1000 组点对点查询文件（**GENERATED，非官方下载**）

### 6.1 官方从未发布 USA 公路网的 `.ss` / `.p2p` 文件

调查证据（详见 §8）：

- Wayback CDX 全站清单（291 条 URL）中，`challenge9/data/**` 下的归档只有 `.gr.gz` / `.co.gz`，
  **不存在** `USA-road-d.*.ss`、`USA-road-d.*.p2p` 等查询文件；
- 官方下载页 2017 年归档（`reference/download.shtml.2017.html`）只列 `.gr.gz` 与 `.co.gz`；
- 旧域名（2006 年归档）同样只有 `samples/sample.ss`、`sample.p2p` 等示例文件；
- 官方基准包 `ch9-1.1` 的设计是让参赛者**用生成器在本地生成**查询文件
  （`ch9-1.1/scripts/genUSA-road-d.*.pl`、`gens/local_q/`），而不是下载成品。

因此：**`USA-road-d.NY.ss` / `.p2p` 作为官方文件无法获得（它们从未被发布/归档）**。
本项目用**官方生成器的参数与格式**在本地生成，并明确标注为 GENERATED，绝不冒充官方数据。

### 6.2 官方生成器与参数（已保存在 `reference/ch9-1.1/scripts/`）

| 脚本 | 作用 | 关键参数 |
| --- | --- | --- |
| `genUSA-road-d.rnd.p2p.pl` | 随机点对点查询 | `NUMQUERY = 1000`、`SEED = 971`；输出 `<dir>/USA-road-d.<REGION>.p2p`，头部 `p aux sp p2p 1000` |
| `genUSA-road-d.loc.p2p.pl` | 局部(local)点对点查询 | `NUMSRC = 100`、`BASERANK = 9`、`SEED = 971`；调用 `gens/local_q/mb.exe`，输出 `USA-road-d.<REGION>.<base-rank>.p2p`（每类 Dijkstra rank 一组） |
| `genUSA-road-d.ss.pl` | 单源查询 | `nsrc = int(10 * n_CTR / n_graph)`（CTR 为最大图 n=14,081,816），源点由 `gens/utils/genSources.pl` 生成 |
| `gens/local_q/` | 局部查询生成器源码 | 见 `gens/local_q/DOC`；按“与源点 Dijkstra rank 相近”的目标点生成查询 |

> ⚠️ **官方脚本的一个细节**：`genUSA-road-d.rnd.p2p.pl` 里虽然写了 `SEED = 971`，
> 但**从未调用 `srand($SEED)`**（Perl 默认自动随机播种），所以官方脚本每次运行产生的
> 1000 对查询都不同、不能复现。为使基准测试可复现，本项目用
> `pq-test/tools/gen-p2p-queries.py` 复刻其**格式与参数**（1000 对、seed 971），
> 但改用 Python 的确定性 Mersenne-Twister RNG：同图 + 同 seed + 同 count ⇒ 输出逐字节可复现。

### 6.3 本项目生成的文件（`data/dimacs/queries/`）

| 文件 | 格式/数量 | 字节 | SHA-256 | 对应图 |
| --- | --- | ---: | --- | --- |
| `USA-road-d.NY.p2p` | `p2p` / 1000 对 | 15,484 | `f2961812991993684dbbad74d86074b240a282ada7f09b13e9120fb93fc5cae9` | d.NY |
| `USA-road-d.BAY.p2p` | `p2p` / 1000 对 | 15,634 | `f236d855e1c6ed4572ce1d1051554e0f8d5e6399822ad5c908bcb1e9ef9750ee` | d.BAY |
| `USA-road-d.NE.p2p` | `p2p` / 1000 对 | 16,816 | `616f43f8e196a3b40c7c6dddb8329afeefadaa8fe6294391292aa3e70fcd68db` | d.NE |
| `USA-road-d.E.p2p` | `p2p` / 1000 对 | 17,718 | `e9ddde03bda4d56a74a4f66011669150509e4fbfc6abe1843e045a732f7c3daa` | d.E |
| `USA-road-t.NY.p2p` | `p2p` / 1000 对 | 15,484 | `f2961812991993684dbbad74d86074b240a282ada7f09b13e9120fb93fc5cae9` | t.NY（与 d.NY 节点数相同 ⇒ 查询对相同） |
| `USA-road-d.NY.ss` | `ss` / 532 源 | 4,889 | `3e03b8b54b350820fbd8c7930cecd84b35a576b7b90b968e0a0af1d42aea7123` | d.NY |
| `USA-road-d.BAY.ss` | `ss` / 438 源 | 4,114 | `c226ff2c67e8cc6c481a0da3c2095f2ea5c576907c88b28cabd2f5143995fe2e` | d.BAY |
| `USA-road-d.NE.ss` | `ss` / 92 源 | 1,167 | `64f4e32ea8f243252b748e3b57154bc5022e4e99635f8a65c28644763835c4ee` | d.NE |
| `USA-road-d.E.ss` | `ss` / 39 源 | 687 | `a6c85abf12296c020978ad46c76c90cf8cefd6d931a02edeb0dbdabfd2e07d67` | d.E |

生成命令（可复现）：

```powershell
python pq-test/tools/gen-p2p-queries.py data/dimacs/USA-road-d.NY.gr        # -> 默认 1000 对, seed 971
python pq-test/tools/gen-p2p-queries.py data/dimacs/USA-road-d.NY.gr --ss   # 单源 .ss（nsrc 按官方公式）
python pq-test/tools/gen-p2p-queries.py graph.gr -o out.p2p --count 500 --seed 7
```

生成文件头部含 `c GENERATED by pq-test/tools/gen-p2p-queries.py ... NOT an official download` 字样。

**C++ 解析器校验结果**（`dimacs-inspect --queries`，全部通过）：

| 查询文件 | 解析结果 |
| --- | --- |
| `USA-road-d.NY.p2p` | kind=p2p, queries=1000 (declared 1000), max source/target id=264069/263868, ids ∈ [1,264346] |
| `USA-road-d.BAY.p2p` | kind=p2p, queries=1000, max source/target=321171/320816, ids ∈ [1,321270] |
| `USA-road-d.NE.p2p` | kind=p2p, queries=1000, max source/target=1522981/1523977, ids ∈ [1,1524453] |
| `USA-road-d.E.p2p` | kind=p2p, queries=1000, max source/target=3593212/3597400, ids ∈ [1,3598623] |
| `USA-road-t.NY.p2p` | kind=p2p, queries=1000, ids ∈ [1,264346] |
| `USA-road-d.NY.ss` | kind=ss, queries=532 (declared 532), max source id=262654 |
| `USA-road-d.BAY.ss` | kind=ss, queries=438, max source id=320833 |
| `USA-road-d.NE.ss` | kind=ss, queries=92, max source id=1469368 |
| `USA-road-d.E.ss` | kind=ss, queries=39, max source id=3450553 |

> 注意：`p aux sp p2p 1000` 中的 1000 是**查询对数量**（每条 `q s t` 一行）。

---

## 7. 目录结构

```
data/dimacs/
├── MANIFEST.sha256                 # sha256sum -c 兼容清单（raw+解压+samples+queries）
├── USA-road-d.NY.gr   /  .co       # P0 解压后明文
├── USA-road-d.BAY.gr               # P1
├── USA-road-d.NE.gr                # P1
├── USA-road-d.E.gr                 # P1
├── USA-road-t.NY.gr                # P2
├── raw/                            # 官方原始 .gz（只读留档，便于重校验）
│   ├── USA-road-d.NY.gr.gz / .co.gz
│   ├── USA-road-d.BAY.gr.gz / NE.gr.gz / E.gr.gz
│   └── USA-road-t.NY.gr.gz
├── samples/                        # 官方示例 .gr/.co/.ss/.p2p/.chk/.res + 文档副本 .txt
├── queries/                        # 本项目生成（GENERATED）的 .p2p / .ss
├── reference/                      # 来源证据：官方基准包、格式文档、CDX/GitHub 清单归档
│   ├── ch9-1.1.tar.gz + ch9-1.1/   # 官方包（仅 docs/format、scripts、gens、README）
│   ├── format.shtml / download.shtml.2017.html / ch9-1.1.README.txt
│   └── evidence/*.json
└── verification/                   # Python 校验 JSON 报告 + C++ 解析器输出存档
```

磁盘占用合计 **415,548,506 B（≈415.5 MB / 396.3 MiB）**：`raw/` 原始 `.gz`
87,090,938 B + 解压纯文本 327,603,114 B + `samples/`/`queries/`/`reference/`/`verification/`/
`MANIFEST.sha256` 约 0.85 MB。
`data/` 已在 `proj2/.gitignore` 中忽略，**不会进入 git**；需要时空目录可由
`fetch-dimacs.ps1` + `gen-p2p-queries.py` 完整重建。

---

## 8. `.ss` / `.p2p` 可用性调查（完整记录）

调查目标：确认能否获得官方的 `USA-road-d.NY.ss`（以及 1000 对查询）。

尝试过的手段与结果：

| # | 手段 | 结果 |
| --- | --- | --- |
| 1 | 官方站点直连（http/https，download.shtml、data 目录） | ❌ HTTP 502 / TLS 握手失败（站点下线） |
| 2 | Wayback CDX 前缀枚举 `diag.uniroma1.it/challenge9/*`（limit 5000，291 条） | 只有 `.gr.gz`/`.co.gz` 与 `samples/sample.{ss,p2p,gr,co,chk,res}`；**无任何 USA 图的 `.ss`/`.p2p`** |
| 3 | Wayback CDX `data/USA-road-d/*`、`data/USA-road-t/*` | 无查询文件；`.co` 只有 CAL/CTR/FLA/NE/NW/W |
| 4 | Wayback CDX 旧域名 `dis.uniroma1.it/~challenge9/*`（309 条，2006 年） | 同样只有 `samples/sample.ss`/`sample.p2p` 及 tiger/rome/代码文件；`code/` 下找到 `ch9-1.1.tar.gz`（后成功下载） |
| 5 | 官方下载页归档（2017、2023 年捕获） | 只列 `.gr.gz`/`.co.gz`，**没有 `.ss`/`.p2p` 下载链接** |
| 6 | GitHub 镜像 `XiangQibu/NEWYORK_DATASET`（API 列文件） | 只有 `NY.{gr,co}.gz`、`t.NY.gr.gz` 及派生的 txt/ipynb，**无 `.ss`/`.p2p`** |
| 7 | grep.app 代码搜索 API、GitHub 代码搜索（未登录不可用）、DuckDuckGo/Bing 网页搜索 | 未发现任何镜像托管的官方 `USA-road-d.NY.ss`/`.p2p`；检索到的都是论文/工具代码，不含数据文件 |
| 8 | 官方基准包 `ch9-1.1.tar.gz`（旧域名 Wayback 2011 捕获，已下载） | ✅ 确认官方做法是**本地生成**：`scripts/genUSA-road-d.rnd.p2p.pl`（1000 对）、`genUSA-road-d.ss.pl`、`gens/local_q/`、`gens/utils/genSources.pl` |

**最终结论**：

1. `USA-road-d.NY.ss` / `USA-road-d.NY.p2p` **官方成品文件不存在于任何可获取的归档中**
   （挑战赛时代就未作为下载文件发布，而是随基准包提供生成器）。
2. 唯一可获取的官方 `.ss`/`.p2p` 实体是 `samples/sample.ss`、`sample.p2p`（已保存，见 §3.3），
   它们只是格式示例（3 源 / 3 对），不能用于 1000 对基准。
3. 本项目所需的 1000 组 source-target 对按官方 `genUSA-road-d.rnd.p2p.pl` 的格式与参数
   （1000 对、seed 971）在本地生成，标记为 GENERATED（§6），并提供可复现生成器。
4. 若需要“官方口径的 local 查询”（按 Dijkstra rank 分档，100 源/档），可用保留的
   `reference/ch9-1.1/gens/local_q/` 源码（`make` 后 `mb.exe <graph-prefix> <num-src> <log2-rank> <seed>`）
   或 `genUSA-road-d.loc.p2p.pl` 生成——但同样属于“生成”而非“官方发布文件”。

---

## 9. 完整数据集（2026-10-28 追加）：全部 48 个 USA 路网文件

2026-10-28 在原有 P0/P1/P2 六个文件基础上，补齐了挑战赛官方提供的**完整 USA 路网数据集**：
12 个区域 × {距离 `d` / 通行时间 `t`} × {图 `.gr` / 坐标 `.co`} = **48 个文件**，原始 `.gz` 与解压
明文全部落盘。官方站点仍下线，数据全部取自 Internet Archive Wayback Machine（旧域名
`dis.uniroma1.it/~challenge9` 的 2011/2012 归档 + 新域名 2023 归档），逐文件在
`fetch-dimacs.ps1` 内嵌表中记录来源时间戳、字节数与 SHA-256。

### 9.1 区域与规模（官方节点/弧数；`d` 与 `t` 为同一张路网的两种权重）

| 区域 | 名称 | 节点 n | 弧 m |
| --- | --- | ---: | ---: |
| USA | Full USA | 23,947,347 | 58,333,344 |
| CTR | Central USA | 14,081,816 | 34,292,496 |
| W | Western USA | 6,262,104 | 15,248,146 |
| E | Eastern USA | 3,598,623 | 8,778,114 |
| LKS | Great Lakes | 2,758,119 | 6,885,658 |
| CAL | California and Nevada | 1,890,815 | 4,657,742 |
| NE | Northeast USA | 1,524,453 | 3,897,636 |
| NW | Northwest USA | 1,207,945 | 2,840,208 |
| FLA | Florida | 1,070,376 | 2,712,798 |
| COL | Colorado | 435,666 | 1,057,066 |
| BAY | San Francisco Bay Area | 321,270 | 800,172 |
| NY | New York City | 264,346 | 733,846 |

### 9.2 体量（2026-10-28 实测）

- 原始 `.gz`（`raw/`）：**2,784,113,022 B（2.59 GiB）** —— 24 个 `.gr.gz` + 24 个 `.co.gz`
- 解压明文（`data/dimacs/` 根）：**9,856,575,358 B（9.18 GiB）**
- 两项合计：**12,640,688,380 B（≈12.64 GB / 11.77 GiB）**；另加 `samples/`、`queries/`、
  `reference/`、`verification/` 与 `MANIFEST.sha256` 约 0.85 MB
- 最大的两张明文图：`USA-road-d.USA.gr` **1,387,075,114 B（1.29 GiB）**、
  `USA-road-t.USA.gr` **1,409,628,748 B（1.31 GiB）**

### 9.3 来源与关键发现

| 文件组 | 来源 | Wayback 时间戳 |
| --- | --- | --- |
| 原 P0/P1/P2 六文件 | 见 §2.3（新域名归档 + GitHub 镜像） | 2023/2024 |
| 新增 40 个文件（12 区域 d 家族 + t 家族，除下面两个 USA 图） | 旧域名 `dis.uniroma1.it/~challenge9` 官方归档 | 2011-06-06 / 2011-06-07，逐文件见脚本内嵌表 |
| `USA-road-d.USA.gr.gz` | 新域名 `diag.uniroma1.it` 归档 | `20230413132821` |
| `USA-road-t.USA.gr.gz` | 旧域名归档 | `20120206064312` |

**与 2026-10-26 结论的差异（重要更正）**：旧版 §9 曾判断 `USA-road-d.USA.co.gz`、`USA-road-t.*`
等“Wayback 未归档、无法获取”。实际原因是当时只查询了新域名 CDX；**旧域名
`dis.uniroma1.it/~challenge9` 的 2011 归档包含完整 48 个文件**（含 `t.*` 的全部 `.co`）。
本次据此补齐。唯一例外：2011-06-06 的 `t.USA.gr.gz` 归档是**截断**的（只存了
320,594,536 B，解压到 320.6 MB 处 gzip CRC 必然失败），已改用 2012-02-06 的完整归档
（payload 358,091,759 B）。

补充说明：官方说明 `.co` 坐标文件对 `d`/`t` 两个家族相同（同一条路网、同一批节点）。实测 `d.X.co`
与 `t.X.co` 的**坐标数据行完全相同**，仅第 3 行注释不同
（`c TIGER/Line nodes coords for graph USA-road-d.X` / `...-t.X`）；`.gz` 压缩后大小多数相等
（LKS / W 相差 1 B），但 SHA-256 不同。两份都保留，便于按官方文件清单逐一对号。

### 9.4 校验结果（2026-10-28，48/48 全部通过）

对 48 个文件逐一执行：

1. gzip 完整解压到 EOF（CRC 校验通过）；
2. `.gr`：`p sp <n> <m>` 与 §9.1 官方表一致，`a` 行数**精确等于** m（USA 图：58,333,344）；
3. `.co`：`p aux sp co <n>` 与官方 n 一致，`v` 行数等于 n；
4. 原始 `.gz` + 解压明文的 SHA-256 写入 `MANIFEST.sha256`。

逐文件 JSON 报告（n/m、行数、字节数、SHA-256）保存在 `data/dimacs/verification/full-set-report.jsonl`（不提交）。

`MANIFEST.sha256` 现在共 **114 条**（48×2 数据文件 + 9 个官方 sample + 9 个生成的 query），
可 `cd data/dimacs && sha256sum -c MANIFEST.sha256` 复核。

### 9.5 使用

```powershell
# 拉取/校验全部 48 个文件（约 2.7 GB 下载；已存在且校验通过时自动跳过）
pwsh pq-test/tools/fetch-dimacs.ps1 -Set Full

# 用 C++ 解析器直接读取全美图
.\build\pq-test\dimacs-inspect.exe data\dimacs\USA-road-d.USA.gr
```

`fetch-dimacs.ps1` 的 `-Set Full` 为本轮新增；默认 `-Set All` 仍是原来的 6 个 P0/P1/P2 文件，
行为不变。新增的 42 条目录项在脚本中标记为 `Priority = 'F'`，每题都带来源 URL、字节数与 SHA-256。

## 10. 参考链接

- 官方挑战赛主页（已下线）：<http://www.diag.uniroma1.it/challenge9/>
- 官方格式说明（Wayback 归档）：`data/dimacs/reference/format.shtml`
- 官方基准包 v1.1（旧域名 Wayback）：<https://web.archive.org/web/20110606172252id_/http://www.dis.uniroma1.it/~challenge9/code/ch9-1.1.tar.gz>
- Wayback CDX API：<https://web.archive.org/cdx/search/cdx>
- GitHub 镜像（NY `.co` 来源）：<https://github.com/XiangQibu/NEWYORK_DATASET>
- 论文/背景：9th DIMACS Implementation Challenge — Shortest Paths（2006）
- 本仓库相关：`pq-test/tools/fetch-dimacs.ps1`（数据重建）、
  `pq-test/tools/verify-dimacs.py`（逐文件校验）、
  `pq-test/tools/gen-p2p-queries.py`（查询生成）、`docs/DIMACS.md`（C++ 解析器文档，Lead 维护）

---

## 11. 变更记录

| 日期 | 变更 |
| --- | --- |
| 2026-10-26 | 首次采集：NY/BAY/NE/E d 图 + NY co + NY t 图；官方示例文件；ch9-1.1 基准包与格式文档；生成 1000 对 p2p 查询；建立 MANIFEST、fetch/verify/gen 脚本；完成 `.ss`/`.p2p` 可用性调查 |
| 2026-10-28 | 补齐完整数据集：12 区域 × d/t × {.gr,.co} = 48 个文件（旧域名 2011/2012 Wayback 归档 + d.USA 2023 归档）；48/48 通过 gzip CRC、n/m 与行数校验；`fetch-dimacs.ps1` 新增 `-Set Full`（42 条新目录项）；MANIFEST 扩至 114 条 |



