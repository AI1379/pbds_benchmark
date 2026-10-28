# proj2 基准测试报告（Windows MSYS2 vs WSL2）

日期：2026-10-28
被测代码：`pq-test/` 下的 heap adaptor 测试 + 三个 benchmark
（`dij-bench-v2` 随机图、`dij-bench-dimacs` DIMACS 路网、
`dij-bench-complexity` 复杂度拟合）。
所有交叉验证的参考实现为 `general-bellman-ford.hpp` 中的 `spfa_slf_lll`。

---

## 1. 环境

| 项目 | Windows | WSL2 |
| --- | --- | --- |
| 发行版 / 运行时 | MSYS2 UCRT64 | Arch Linux（WSL2，kernel 6.6.87.2-microsoft-standard） |
| 编译器 | g++ 16.2.0 | g++ 16.2.1 |
| CMake / 构建系统 | CMake + Ninja | CMake 4.4.3 + Ninja 1.13.2 |
| CPU | Intel Core i9-14900HX，32 逻辑核（P+E） | 同一物理机，WSL 可见 32 逻辑核 |
| 内存 | 31.6 GB | 25 GB（WSL 限额） |
| 构建类型 | Release（`-O3 -DNDEBUG`） | Release（`-O3 -DNDEBUG`） |

说明：

- 两个环境是**同一台物理机、不同时间**跑的（Windows 结果来自前一轮，WSL 结果为本次新跑），
  因此下面的“WSL/Windows 比值”包含机器负载差异，不能当作纯粹的 OS 性能对比。
- WSL 的数据从 `/mnt/d`（9p 文件系统）读取，解析明显比 Windows 原生盘慢（见 §6）。
- WSL 运行时 load average 约 0.6–1.0；Windows 那轮机器上有其他任务，计时抖动更大
  （benchmark 输出里 CPU time 和 real time 经常明显分叉）。

---

## 2. 正确性测试结果

| 测试 | 结果 |
| --- | --- |
| WSL Release：`heap-adaptor-test` | **85/85 通过**（28 ms，death tests 被 NDEBUG 关闭） |
| WSL Debug：`heap-adaptor-test` | **89/89 通过**（含 4 个 death test，233 ms） |
| Windows Release：`heap-adaptor-test` | **85/85 通过** |
| Windows ASan+UBSan（clang/MSVC target） | **49/49 通过**（含 death test） |
| 三个 benchmark 启动时的 cross-check | Windows / WSL 均通过（参考 = `spfa_slf_lll`） |
| `general-bellman-ford` / `spfa_slf_lll` 差分测试 | 随机图 + DIMACS NY 5 源点，与 Dijkstra / FIFO Bellman-Ford 全 `dis` 一致 |

备注：Windows Debug 直跑 death test 会因 gtest 在 `%TEMP%` 建临时文件报 error 5（环境 ACL 问题），
所以 Windows 的 death test 只在 ASan runner（重定向 TEMP）里跑过。

---

## 3. DIMACS 路网基准

每个 benchmark iteration = 跑完整个查询集；表中为**平均单查询耗时**。
（DIMACS case 都只有 1 个 iteration，没有 repetitions，见 §8 的局限说明。）

### 3.1 USA-road-d.NY（n=264,346 / m=733,846），1000 条 p2p 查询

| 实现 | WSL ms/query | Windows ms/query | WSL 相对 std | Windows 相对 std |
| --- | ---: | ---: | ---: | ---: |
| dijkstra_std | 20.96 | 21.43 | 1.00× | 1.00× |
| dijkstra_binary | **20.03** | 21.85 | **0.96×** | 1.02× |
| dijkstra_pbds_pairing | 31.59 | 39.15 | 1.51× | 1.83× |
| dijkstra_segt | 40.61 | 40.84 | 1.94× | 1.91× |
| dijkstra_pbds_thin | 40.07 | 59.80 | 1.91× | 2.79× |
| dijkstra_fib | 48.07 | 56.28 | 2.29× | 2.63× |
| spfa（FIFO） | 273.51 | 378.61 | 13.05× | 17.67× |
| **spfa_slf_lll** | **8.06** | **15.02** | **0.38×** | **0.70×** |

### 3.2 USA-road-d.BAY（n=321,270 / m=800,172），前 200 条 p2p 查询

| 实现 | WSL ms/query | Windows ms/query | WSL 相对 std | Windows 相对 std |
| --- | ---: | ---: | ---: | ---: |
| dijkstra_std | 22.27 | 41.21 | 1.00× | 1.00× |
| dijkstra_binary | 22.47 | 47.57 | 1.01× | 1.15× |
| dijkstra_pbds_pairing | 32.88 | 79.34 | 1.48× | 1.93× |
| dijkstra_segt | 46.47 | 76.21 | 2.09× | 1.85× |
| dijkstra_pbds_thin | 44.46 | 99.86 | 2.00× | 2.42× |
| dijkstra_fib | 55.92 | 114.15 | 2.51× | 2.77× |
| spfa（FIFO） | 369.91 | 792.38 | 16.61× | 19.23× |
| **spfa_slf_lll** | **10.50** | **18.20** | **0.47×** | **0.44×** |

### 3.3 结论

- 两种环境下**相对排序一致**：路网上 `std` / 手写二叉堆最快；`pairing` 约 1.5×；
  `segt` / `thin` 约 2×；`fib` 约 2.3–2.5×；**FIFO SPFA 约 13–17×，不可用**；
  **SLF+LLL 版只要 std 的 0.38–0.47（快 2.1–2.6×）**。
- SLF+LLL 与 FIFO 的差距达到 **34–35×**（NY：273.5 / 8.06；BAY：369.9 / 10.5），
  这是 SLF/LLL 把每弧扫描次数从几十/几百次压到 ~1 次带来的，不是实现细节的常数差异。
- 之前 Windows 上 E 图（n=3.6M / m=8.8M）的调查数据也同向：Dijkstra 757 ms/query，
  SLF+LLL 334 ms/query，FIFO SPFA 107 s/query。

---

## 4. 随机图基准（`dij-bench-v2`）

参数扫描沿用 legacy `dij-bench` 的 `(n, m, wmax=200000)` 序列；下面取三组代表规模，
数值为**单次 Dijkstra 的耗时（ms）**（每条纹理由足够多 iteration 平均）。

| 实现 | 2560 / 327552 | 40960 / 327672 | 163840 / 327678 |
| --- | ---: | ---: | ---: |
| dijkstra_std | 1.055 | 8.122 | 14.843 |
| dijkstra_binary | 0.554 | 6.463 | 20.048 |
| dijkstra_segt | 0.585 | 6.760 | 26.206 |
| dijkstra_fib | 0.833 | 12.288 | 47.863 |
| dijkstra_pbds_pairing | 0.736 | 9.927 | 28.915 |
| dijkstra_pbds_thin | 1.046 | 13.480 | 36.105 |
| spfa（FIFO） | 0.798 | 4.120 | **5.356** |
| **spfa_slf_lll** | **0.665** | **3.970** | **4.773** |

v2 的 `(n, m)` 阶梯不适合做复杂度拟合（RMS 经常 >100%）；专门的固定密度复杂度实验见 §5。

观察：

- 随机稀疏图上 **SPFA 家族最快**（没有堆开销，且几乎只松弛一遍）；
  SLF+LLL 比 FIFO 略好或相当（大稀疏规模 4.77 vs 5.36 ms）。
- 稠密小图（n=2560, m≈n²/2）上手写二叉堆 / 段树堆 / SLF+LLL 表现最好，
  `std::priority_queue` 和 `thin heap` 的交换/常数开销明显。
- 大稀疏图（n=163840, m≈2n）上 `std::priority_queue` 的二叉堆反而比手写二叉堆快
  （14.8 vs 20.0 ms），这和堆的删除/交换常数及访问模式有关。

---

## 5. 复杂度拟合（Google Benchmark Complexity）

普通的 `dij-bench-v2` 沿用了 legacy 的 `(n, m)` 阶梯，m 的跳变不规则，直接让
`->Complexity(...)` 拟合噪声很大（RMS 经常 >100%）。为了得到可解释的复杂度曲线，
新增 [dij-bench-complexity.cpp](../../pq-test/dij-bench-complexity.cpp)，用
[random-graph.hpp](../../pq-test/random-graph.hpp) 构造两族固定 n–m 关系的图：

| 家族 | 边数 | n 范围 | 拟合模型 |
| --- | --- | --- | --- |
| sparse | `m = 4n`（固定平均出度 4） | `2^10 .. 2^17` | `N lg N` |
| dense | `m = n(n-1)/4` | `2^7 .. 2^12` | `N^2` |

每个 benchmark 都调用 `state.SetComplexityN(n)`，并固定 `->Complexity(...)` 模型；
Google Benchmark 用最小二乘拟合 `T = c·f(n)`，输出系数 `_BigO` 和按均值归一化的
`_RMS`。graph 在计时循环外构建，同族所有实现用同一张图（固定 seed），启动时的
SLF+LLL cross-check 通过。

**为什么不用 `oAuto`**：`oAuto` 的残差是**绝对时间**，会被最大的 n 点主导，选择不稳定
（例如 sparse FibonacciHeap 有时会从 `NlgN` 翻成 `N^2`，且系数显示为 0.00）；
固定模型的拟合更稳定、可直接横向比较。dense 的真实模型是 `O(n² log n)`，其中
`log n` 在范围内只变化约 1.6 倍，用 `N^2` 拟合的 RMS 也足够小。

### 5.1 sparse：m = 4n（WSL，系数单位 ns / (n·log₂n)）

| 实现 | WSL 系数 | WSL RMS | Windows 系数 | Windows RMS |
| --- | ---: | ---: | ---: | ---: |
| dijkstra_std | **9.33** | 8% | 11.56 | 23% |
| dijkstra_binary | 9.80 | 12% | 19.81 | 22% |
| dijkstra_segt | 10.69 | 13% | 23.72 | 27% |
| dijkstra_pbds_pairing | 15.36 | 16% | 33.81 | 21% |
| dijkstra_pbds_thin | 19.58 | 11% | 42.46 | 36% |
| dijkstra_fib | 21.86 | 18% | 46.55 | 12% |
| spfa（FIFO） | 4.46 | 10% | 7.25 | 14% |
| spfa_slf_lll | **3.46** | 16% | 7.11 | 26% |

- 8 个实现都落在 `NlgN` 上，与“固定平均出度时 Dijkstra 是 O(m log n) = O(n log n)”一致。
- 系数排序和实测速度完全对应：std < binary < segt < pairing < thin < fib；
  SPFA 两个变体系数最小（随机稀疏图上基本一遍扫描，没有堆开销）。
- SPFA 的 `NlgN` 是**经验拟合**：其扫描量理论上是 Θ(m)=Θ(n)，但 n 增大时
  relabel 次数和缓存 miss 让增长略高于线性；`NlgN` 拟合明显比 `N` 更贴合，
  因此报告里按实际拟合结果标注。

### 5.2 dense：m = n(n-1)/4（系数单位 ns / n²）

| 实现 | WSL 系数 | WSL RMS | Windows 系数 | Windows RMS |
| --- | ---: | ---: | ---: | ---: |
| dijkstra_std | 0.42 | 2% | 0.71 | 3% |
| dijkstra_binary | **0.31** | 11% | 0.65 | 15% |
| dijkstra_segt | **0.31** | 8% | 0.66 | 18% |
| dijkstra_pbds_pairing | 0.36 | 5% | 0.86 | 2% |
| dijkstra_pbds_thin | 0.42 | 7% | 0.79 | 2% |
| dijkstra_fib | 0.34 | 6% | 0.69 | 8% |
| spfa（FIFO） | 0.86 | 26% | 1.72 | 25% |
| spfa_slf_lll | 0.63 | 19% | 1.24 | 18% |

- 稠密图上边扫描量 m = Θ(n²) 主导，所有实现都按 `N^2` 增长，堆实现的 RMS 只有 2–11%，
  验证了“稀疏度固定时，Dijkstra 的时间由边数主导”。
- SPFA 两个变体的系数更大、RMS 更高（19–26%），说明稠密随机图上 relabel 次数随 n
  增长得更快，单纯的 `N^2` 模型不足以完全描述（真实模型更接近 `n² log n` 的常数波动）。
- 同样 WSL 的系数/RMS 整体优于 Windows（机器负载差异），但两边的排序一致。

---

## 6. Windows vs WSL 的差异

| 现象 | 解释 |
| --- | --- |
| WSL 大部分数值比 Windows 低（比值 0.4–1.0，BAY 最明显） | Windows 那轮机器负载更高（CPU time vs real time 分叉、RMS 大）；WSL 轮次负载 0.6–1.0，CPU time≈real time |
| 相对排序一致 | 说明结论（SLF+LLL 最好、FIFO SPFA 不可用、std/binary 领先）与平台无关 |
| WSL 解析 `.gr` 慢：NY 0.458 s vs Windows 0.089 s；BAY 1.037 s vs 0.100 s | 数据在 `/mnt/d`（9p），不是 ext4；解析在计时区间外，只影响启动时间；大图建议把数据拷到 WSL 家目录 |
| WSL Random benchmark 第 1 个 case 有 73 ns 的异常小值（slf_lll/10/9） | 首次调用包含预热/页缓存效应，且 n 很小，绝对时间太短，不必解读 |

---

## 7. 总结

1. **功能正确性**：WSL Release 85/85、WSL Debug 89/89（含 death tests）、Windows 85/85、
   Windows ASan 49/49；三个 benchmark 的跨实现 cross-check（参考 `spfa_slf_lll`）通过。
2. **路网数据**：Dijkstra 系里 std / 手写二叉堆最好；`pairing`、`segt`、`thin`、`fib`
   依次变慢；**FIFO SPFA 比 std 慢 13–17×（NY/BAY），不可用于路网**；
   **SLF+LLL SPFA 比 std 快约 2.1–2.6×，比 FIFO SPFA 快约 34–35×**。
3. **随机图数据**：SPFA 家族（尤其 SLF+LLL）最快，Dijkstra 系里稠密小图用手写二叉堆更好，
   大稀疏图 std 堆更好；`fib` 常数最大。
4. **复杂度拟合**：sparse（m=4n）全部实现拟合为 `NlgN`（WSL RMS 8–18%），
   系数排序与实测速度一致；dense（m=n(n−1)/4）堆实现拟合为 `N^2`（RMS 2–11%），
   说明边数主导；复杂度的详细数据见 §5。
5. **平台结论**：WSL2 与 Windows 上排序一致；WSL 本次数字整体更平稳（负载更低、
   CPU time≈real time），但两者结果都受机器负载影响，报告中的绝对值应看作趋势。

---

## 8. 局限

- DIMACS 的每个 benchmark case 只有 **1 个 iteration**（一次就是 1000/200 条查询），
  没有 repetitions；随机图 case 由 Google Benchmark 自动迭代多轮。
- Windows 与 WSL 不是同一时刻跑的，机器负载不同；跨平台比值只作参考。
- 只在 NY（1000 条）和 BAY（200 条）上跑了完整 benchmark；NE/E 只做过小样本调查；
  全美（USA，n≈23.95M）数据未下载、未跑。
- 复杂度拟合固定了模型（sparse `NlgN`、dense `N^2`）：`oAuto` 的最小二乘残差是绝对
  时间，被最大 n 主导，稀疏 FibonacciHeap 会偶发地把 `NlgN` 翻成 `N^2`（系数≈0），
  所以没有采用 `oAuto`；系数和 RMS 都基于 CPU time，且 dense 的真实模型是
  `O(n² log n)`，用 `N^2` 近似后 SPFA 的 RMS 偏高（19–26%）。
- 正式报告建议：空闲时在 WSL 原生盘（先把 `data/dimacs` 复制到 ext4）重跑，
  加 `--benchmark_repetitions=3 --benchmark_report_aggregates_only=true`；
  大图用 `--limit` 并排除 FIFO `spfa` case（它的参考/计时在大图上不可行）。

---

## 9. 复现步骤

Windows（MSYS2 UCRT64 / g++ 16.2.0，Release）：

```powershell
cd ads-projects\proj2
cmake --build build --target heap-adaptor-test dij-bench-v2 dij-bench-dimacs dij-bench-complexity
.\build\pq-test\heap-adaptor-test.exe
.\build\pq-test\dij-bench-v2.exe --benchmark_out=dij-v2.json --benchmark_out_format=json
.\build\pq-test\dij-bench-dimacs.exe --benchmark_out=dimacs-NY.json --benchmark_out_format=json
.\build\pq-test\dij-bench-dimacs.exe --gr data\dimacs\USA-road-d.BAY.gr --limit 200
.\build\pq-test\dij-bench-complexity.exe --benchmark_out=complexity.json --benchmark_out_format=json
```

WSL2（Arch / g++ 16.2.1，Release，构建在 WSL 家目录）：

```bash
export HOME=/home/arendellian13
cmake -S /mnt/d/Projects/ACTraining/ads-projects/proj2 \
      -B "$HOME/proj2-build-wsl" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$HOME/proj2-build-wsl" --target heap-adaptor-test dij-bench-v2 dij-bench-dimacs dij-bench-complexity -j 16

"$HOME/proj2-build-wsl/pq-test/heap-adaptor-test"
cd /mnt/d/Projects/ACTraining/ads-projects/proj2
"$HOME/proj2-build-wsl/pq-test/dij-bench-v2" \
    --benchmark_out=/mnt/d/Projects/ACTraining/.ads-tmp/bench-results/wsl/dij-v2-full.json \
    --benchmark_out_format=json
"$HOME/proj2-build-wsl/pq-test/dij-bench-dimacs" \
    --benchmark_out=/mnt/d/Projects/ACTraining/.ads-tmp/bench-results/wsl/dimacs-NY-full.json \
    --benchmark_out_format=json
"$HOME/proj2-build-wsl/pq-test/dij-bench-dimacs" \
    --gr data/dimacs/USA-road-d.BAY.gr --limit 200
"$HOME/proj2-build-wsl/pq-test/dij-bench-complexity" \
    --benchmark_out=/mnt/d/Projects/ACTraining/.ads-tmp/bench-results/wsl/complexity-wsl.json \
    --benchmark_out_format=json
```

Debug（含 death tests）WSL：

```bash
cmake -S /mnt/d/Projects/ACTraining/ads-projects/proj2 \
      -B "$HOME/proj2-build-wsl-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build "$HOME/proj2-build-wsl-debug" --target heap-adaptor-test -j 16
"$HOME/proj2-build-wsl-debug/pq-test/heap-adaptor-test"
```

---

## 10. 原始数据

| 来源 | 位置 |
| --- | --- |
| Windows 结果 JSON | `.ads-tmp/bench-results/dij-v2-full.json`、`dimacs-NY-full.json`、`dimacs-BAY-200-full.json`、`complexity-windows.json`（+`complexity-windows.txt`） |
| WSL 结果 JSON + 控制台日志 | `.ads-tmp/bench-results/wsl/`（`dij-v2-full.json/.txt`、`dimacs-NY-full.json/.txt`、`dimacs-BAY-200-full.json/.txt`、`complexity-wsl.json/.txt`） |
| 复杂度拟合源码 | `pq-test/dij-bench-complexity.cpp`（测试数据生成在 `pq-test/random-graph.hpp`） |

---

## 11. 附录：Fibonacci 堆在稠密图上的量级探测

为了回答“Fib 堆在什么规模上会有优势”，用 `.ads-tmp/fib-scale-probe/probe.cpp`
在 **m = n(n−1)/4 的稠密随机图**上做了补充测量（WSL，Release，每项 5 次取最小；
n=32768 时图本身占 ~4.3 GB）：

| n | m | 最快实现 | Fib ms | Fib 相对最快 |
| --- | ---: | --- | ---: | ---: |
| 2 048 | 1 048 064 | binary 0.97 ms | 1.38 ms | +42% |
| 4 096 | 4 193 280 | segt 5.80 ms | 6.76 ms | +17% |
| 8 192 | 16 775 168 | binary 23.18 ms | 25.81 ms | +11% |
| 16 384 | 67 104 768 | binary 87.13 ms | 94.61 ms | +8.6% |
| 32 768 | 268 427 264 | pairing 395.84 ms | 420.98 ms | +6.4% |

- 稠密图上 Fib 与最优堆的差距随 n 增大而缩小（从 +42% 缩小到 +6%），但**始终没有
  在可行规模内赢过二进制堆/段树堆/pairing 堆**；n=65536 时仅边数组就要 ~17 GB
  （生成时还要临时数组），没有实际可跑性。
- 稀疏图（路网、m=4n 随机图）上 Fib 稳定慢 2.3–2.6×，因为它每次操作的指针跳转/
  consolidate 常数远大于数组式二叉堆，decrease-key 省下的 log n 完全补不回来。
- 理论上的 Fib 优势区间（m≈n²、n 很大，或 decrease-key 次数远大于 n 的人为负载）
  在课程作业的实际数据规模内不可达；这也是 9th DIMACS 挑战赛优胜实现选择
  bucket/radix heap 而不是 Fibonacci heap 的原因（见 §5 的复杂度拟合与 §3 的路网数据）。
- **radix heap / 多级桶** 已在 `pq-test/radix-heap.hpp` 实现，并接入三个基准程序
  （基准名为 `dijkstra_radix`）。它是 Dial 算法的推广（按位前缀分桶的
  O(m + n log C) 实现，属于单调优先队列）：Dijkstra 的出堆键单调不减 + 路网权重是
  非负整数，正好满足它的单调插入要求；预期在 NY/BAY 这类路网上能明显快过二叉堆，
  也比 FIFO SPFA 和 Fib 堆更合适。本报告还没有这一项的数据。

