# Fibonacci 堆：原理、C++ 实现要点与 proj2 接入指南

> 目标读者：proj2 的作者本人（已会二叉堆、写过 Dijkstra，正在把 `pq-test` 重构成
> `dijkstra_bench` 架构）。
>
> 本文是**讲解 + 设计蓝图**，不是成品实现：所有核心操作只给 CLRS 风格伪代码、
> 数据布局取舍、接口契约和测试/实验方案，方法体由你自己写。
>
> 文中给出的 `FibonacciHeap` **类声明骨架**已用 g++ 16.2（MSYS2 UCRT64，
> `-std=c++20 -Wall -Wextra -Wpedantic`）对着你的 `heap-adaptor.hpp` 编译验证过，
> 说明 CRTP 契约本身是成立的；真正的方法体留空即可。

---

## 目录

- [0. 结论先行](#0-结论先行)
- [1. 背景：Dijkstra 的操作序列与堆的复杂度](#1-背景dijkstra-的操作序列与堆的复杂度)
- [2. 结构定义与不变量](#2-结构定义与不变量)
- [3. 六个操作](#3-六个操作)
- [4. 摊还分析](#4-摊还分析)
- [5. 面向 proj2 的 C++ 实现设计](#5-面向-proj2-的-c-实现设计)
- [6. 接入 proj2 的步骤](#6-接入-proj2-的步骤)
- [7. 测试与验收清单](#7-测试与验收清单)
- [8. 参考资料](#8-参考资料)

---

## 0. 结论先行

1. **Fib 堆的价值**：`insert` / `union` / `decrease-key` 都是 **O(1) 摊还**，
   `extract-min` 是 **O(log n) 摊还**。Dijkstra 若用「懒插入 + decrease-key」策略，
   总复杂度是 **O(E + V log V)**，而二叉堆是 **O(E log V)**。
2. **摊还代价的来源**：势函数 `Φ = 根表节点数 + 2 × 被 mark 的节点数`。
   `decrease-key` 里「级联剪切」花的 O(c) 时间由势能下降支付；
   `extract-min` 里「合并根表」花的 O(t) 时间由插入时攒下的势能支付。
3. **结构上最重要的两件事**：
   - 根表非常"松"，插入就是往根表挂一个新树，**不做任何合并**；
   - 非根节点有一个 `mark` 位：失去第一个孩子时只标记，失去第二个孩子时被剪到根表
     （级联剪切）。mark 机制保证任意节点的孩子数 ≤ `log_φ n`。
4. **接入 proj2 的工作量**：新增 `pq-test/fib-heap.hpp`（实现你 `BaseHeapAdaptor`
   的 5 个私有 `_impl` 方法）+ `pq-test/fib-heap-test.cpp`（gtest 对拍）+
   一个通用 `general-dijkstra.hpp` + `csr-graph.hpp` 的 builder + 新 benchmark 入口。
   **不需要改 `heap-adaptor.hpp` 的接口**（现有设计已经够用）。
5. **工程上最大的简化**：因为你的 timestamp 优化保证了「同一 epoch 内每个顶点最多
   一个活跃条目」，所以 **节点索引可以直接用顶点号**：handle 仍然存在，
   但它就是顶点号本身（隐式 handle，见 §5.1），不需要额外的 handle 表，也不需要
   节点池 + free list。代价是 O(n) 的节点数组（见 §5.1 的取舍）。
6. **心态准备**：Fibonacci 堆在 DIMACS 路网这种稀疏整数权图上，**实测很可能输给
   二叉堆 / 4 叉堆 / 段树堆**（指针追逐、cache miss、常数大）。这是文献里公认的结论，
   也正是你报告里可以写的分析点。你的任务不是"证明 Fib 堆最快"，而是
   "实现正确 + 用统一 harness 公平测出来 + 解释为什么"。

---

## 1. 背景：Dijkstra 的操作序列与堆的复杂度

### 1.1 一次 Dijkstra 到底做了哪些堆操作

在「懒插入 + decrease-key」的写法里（也就是你新 `BaseHeapAdaptor` 的设计意图）：

```
dis[s] = 0; heap.insert(s)
while heap 非空:
    (d, u) = heap.extract_min()
    for (u, v, w) in E:
        if dis[v] > d + w:
            dis[v] = d + w
            heap 里若 v 不存在: heap.insert(v, dis[v])
            否则:              heap.decrease_key(v, dis[v])
```

所以操作次数是：

| 操作 | 次数上界 | 说明 |
| --- | --- | --- |
| `insert` | ≤ n | 每个顶点第一次被松弛时插入 |
| `decrease-key` | ≤ m | 每次成功松弛一次 |
| `extract-min` | ≤ n | 每个顶点出堆一次 |

注意：**没有 decrease-key 也能跑 Dijkstra**（`std::priority_queue` 的懒删除写法就是
每次松弛都 push 一份，pop 到过期条目就跳过），但那样 `push` 次数 ≤ m，
总复杂度 O(m log n)。

### 1.2 各种堆的理论复杂度

| 操作 | 二叉堆 `std::priority_queue` | 配对堆 / Thin 堆（pb_ds） | **Fibonacci 堆** |
| --- | --- | --- | --- |
| `insert` | O(log n) | O(1) 摊还 | **O(1) 摊还** |
| `minimum` | O(1) | O(1) | **O(1)** |
| `extract-min` | O(log n) | O(log n) 摊还 | **O(log n) 摊还** |
| `decrease-key` | O(log n) | O(log n) 摊还 | **O(1) 摊还** |
| `union` / `meld` | O(n) | O(1) | **O(1)** |

> 配对堆/Thin 堆的 decrease-key 摊还界在不同文献里有不同说法，这里按
> "≈ O(log n) 摊还、常数比 Fib 小"理解即可；写报告时以 pb_ds 文档或原始论文为准。

代入 Dijkstra：

- 二叉堆：`O(n log n + m log n)` = **O(E log V)**；
- Fib 堆：`O(n + m + n log n)` = **O(E + V log V)**。

### 1.3 理论优势 vs. 实际常数（重要，别被 O 记号骗了）

Fib 堆的 O(1) 摊还操作不是免费的：

- **摊还 ≠ 每次**：`extract-min` 时要一次性合并根表，单次最坏 O(n)；
- **常数大**：每个节点 5 个指针 + key + degree + mark；大量指针追逐导致 cache miss；
- **内存大**：每节点 32–56 字节，是二叉堆数组表示的 4–7 倍；
- **路网图的特性**：m ≈ 2.4n（稀疏）、权重是小整数、decrease-key 次数不算多，
  与 Fib 堆的"优势区间"不完全重合。

文献与 DIMACS 挑战赛的实测结论普遍是：**路网 + 整数权重的 Dijkstra 里，
binary/4-ary heap、radix heap、甚至段树堆的墙钟时间往往优于 Fib 堆**。
所以：

- 你的实验设计一定要**统一 harness、统一图表示、统一 seed**，否则数字没有说服力；
- 报告里要解释"为什么理论更优的数据结构实测更慢"（常数、cache、操作分布），
  这反而是一个加分项。

### 1.4 proj2 的要求与你现在的架构

`docs/PROJECT.md` 的要求：

- 至少两种堆，**Fibonacci 堆必须包含**（6 分）；
- 用 DIMACS USA road networks 评测（数据只给下载链接）（2 分）；
- 至少 1000 对 query 计时（2 分）；
- 画时间-规模图（2 分）；
- 充分注释（3 分）；
- 报告文档（1+2+1 分）。

你已经在 `pq-test/` 下开了新架构（今天的未提交文件）：

```
pq-test/
├── heap-adaptor.hpp      # 堆统一接口：BaseHeapAdaptor<Derived, Decreaseable>
├── general-dijkstra.hpp  # 通用 Dijkstra 模板（目前只有 fill dis 的 stub）
├── csr-graph.hpp         # CSR 图（目前只有 operator[]，缺 builder）
├── segment-tree-heap.hpp # 段树堆（stub）
└── (旧) dijkstra-*.hpp / dij-bench.cpp  # 每堆一个全局 namespace 的旧写法
```

新架构下，一个堆 = 一个 `BaseHeapAdaptor` 的派生类；Dijkstra 只写一次，
用不同 `Heap` 实例化。Fibonacci 堆就是新增一个 `fib-heap.hpp` 插进去。
下面 §2–§4 讲原理，§5 讲实现设计，§6 讲具体怎么接入。

---

## 2. 结构定义与不变量

### 2.1 节点

每个节点（CLRS 19.1）：

| 字段 | 含义 |
| --- | --- |
| `key` | 键值（你的 `int64_t` 距离） |
| `parent` | 父节点；根为 NIL |
| `child` | 某个孩子（孩子链表头）；无孩子为 NIL |
| `left` / `right` | 兄弟环 / 根表环里的左右邻居（**双向循环链表**） |
| `degree` | 孩子个数 |
| `mark` | 自上次成为别人的孩子以来，是否失去过一个孩子 |

**关键约定：所有链表都是循环的（circular）**，且没有哨兵：

- 一个根节点的 `left/right` 指向根表里前后相邻的根；
- 一个孩子的 `left/right` 指向同一父节点的兄弟；
- 只有一个元素时，`left == right == 自己`。

这样插入/删除/拼接都是 O(1) 的常数条指针操作。

### 2.2 堆对象

```
FibonacciHeap
├── min_   : 指向根表里 key 最小的节点（空堆为 NIL）
├── size_  : 节点数（可选，但调试/断言很有用）
└── nodes_ : 节点数组（下一节讲为什么可以按顶点号索引）
```

### 2.3 结构示意

```
min_ ──► (3) ⇄ (5) ⇄ (12) ⇄ (7)      ← 根表：循环双向链表，各树之间无顺序关系
          │
          └──► (4) ⇄ (9)              ← child 表同样循环双向
                 │
                 └──► (11)
```

每棵树都满足**小顶堆序**：父的 key ≤ 孩子的 key。但**树与树之间完全不比大小**，
`min_` 只是根表里最小的那个根。

### 2.4 五条不变量

实现时请把它们写成 debug 断言 / `validate()` 检查器：

1. **堆序**：对任意非根节点 x，`key[parent[x]] ≤ key[x]`。
2. **根表**：所有根 `parent == NIL`；`min_` 是根表中 key 最小的根（空堆时 NIL）。
3. **孩子表**：`degree[x]` 等于 x 的实际孩子数；每个孩子的 `parent == x`；
   孩子表与根表都是闭合的循环双向链表。
4. **mark**：**根节点的 mark 恒为 false**；非根节点的 mark 为 true
   当且仅当它自"上次被 link 成某个节点的孩子"以来，失去过一个孩子。
5. **度数上界**：任意节点的 `degree ≤ D(n) = ⌊log_φ n⌋`（φ = (1+√5)/2 ≈ 1.618；
   n ≤ 2^32 时 D(n) ≤ 46）。这条不是必须手动维护的，而是 mark 机制保证的，
   但它是 `consolidate` 里数组大小的依据。

### 2.5 mark 到底在防什么

如果 decrease-key 把节点剪到根表时**不做级联**，一棵树可能被剪成一条长链，
`extract-min` 把根的孩子搬到根表后，`consolidate` 的度数就可能很大 → 摊还分析失效。

mark 规则是：**每个非根节点一生只能"失去一个孩子"而不受罚**；
失去第二个孩子时，它自己也会被剪到根表（并且这个动作可能继续向上级联）。
这条规则配合"link 只发生在度数相同的根之间"，可以归纳证明度上界（§4.3），
从而保证 `consolidate` 的数组是 O(log n) 大小、`extract-min` 是 O(log n) 摊还。

---

## 3. 六个操作

记号：`NIL = kNull`，`A[d]` 是 consolidate 用的度数桶。以下伪代码是 C++ 风格的，
可以直接翻译，但建议先自己按 CLRS 推一遍再写。

### 3.1 INSERT / MINIMUM / UNION

```text
INSERT(H, x):                       // O(1)
  x.parent = NIL; x.child = NIL
  x.left = x.right = x; x.degree = 0; x.mark = false
  把 x 挂到根表（例如 min_ 的右边）
  若 min_ == NIL 或 x.key < min_.key: min_ = x
  H.n++

MINIMUM(H): return H.min_           // O(1)

UNION(H1, H2):                      // O(1)
  把两个根表拼成一个循环表；min_ 取两者较小
  （你的 adaptor 用不到 UNION，但报告里可以提一句）
```

要点：**insert 完全不合并**。这是 Fib 堆"懒"的根源，也是它在 Dijkstra 里
能以 O(1) 摊还插入的原因——代价都推给后面的 extract-min 了。

### 3.2 EXTRACT-MIN

```
EXTRACT-MIN(H):
  z = min_
  把 z 的所有孩子"升级"为根：parent = NIL, mark = false
  把这一圈孩子拼接进根表（可以就插在 z 原来的位置）
  把 z 从根表删除
  min_ 先随便指向一个根（z 已删，根表非空的话）
  CONSOLIDATE(H)                    // 合并同 degree 的树，重建根表，重选 min_
  H.n--
  return z
```

细节：

- 孩子变根时必须 **parent = NIL 且 mark = false**（根不能带 mark）；
- 如果删掉 z 后堆空了，直接返回，不要 consolidate；
- `CONSOLIDATE` 是唯一 O(log n) 的摊还部分（见下）。

伪代码（"干净的写法"，见 §5.4 的 C++ 版本）：

```
CONSOLIDATE(H):
  把根表节点快照到 roots[]（先不要边遍历边改链表！）
  清空 A[0..kMaxDegree-1]
  for r in roots:
      x = r; d = degree[x]
      while A[d] != NIL:
          y = A[d]
          if key[y] < key[x]: swap(x, y)
          LINK(y, x)               // y 从根表摘下，成为 x 的孩子
          A[d] = NIL; d++
      A[d] = x
  用 A 里非 NIL 的节点重建根表；min_ = 其中 key 最小者

LINK(y, x):                         // 前提：x、y 都是根，degree 相同
  把 y 从根表删除
  y.parent = x; y.mark = false
  把 y 挂进 x 的 child 表（x.degree++）
```

为什么 while 循环总会停：每次 link 会让 x 的度数 +1，而 A 的槽位有限
（度数不超过 D(n)），所以最多 O(D(n)) 次。

### 3.3 DECREASE-KEY / CUT / CASCADING-CUT

```
DECREASE-KEY(H, x, k):
  assert k ≤ key[x]
  key[x] = k
  p = parent[x]
  if p != NIL and k < key[p]:
      CUT(H, x)
      CASCADING-CUT(H, p)
  else if p == NIL and k < key[min_]:
      min_ = x                       // 减少的是根且成了新最小

CUT(H, x):                          // 把 x 剪到根表，O(1)
  p = parent[x]
  从 p 的 child 表里摘掉 x；若 p.child == x 则 p.child 改为 x.right（无孩子则 NIL）
  p.degree--
  x.parent = NIL; x.mark = false
  把 x 挂进根表；若 key[x] < key[min_]: min_ = x

CASCADING-CUT(H, y):
  while y != NIL:
      if y.mark == false:
          y.mark = true              // 第一次失去孩子：只标记，停止
          break
      p = parent[y]                  // 先存父指针
      CUT(H, y)                      // 第二次：y 自己被剪到根表
      y = p
```

要点：

- `decrease-key` 本身只做 O(1) 的 key 修改；只有当它破坏了堆序才 cut；
- **每个被级联剪掉的节点都是 marked 节点**（所以每次剪枝至少消耗一个 mark）；
- cut 之后节点是根，**mark 必须清零**（根不能带 mark）；
- 级联剪切写成**迭代**（while 循环），不要递归——递归深度虽然只有 O(log n) 量级，
  但没必要，而且 debug 时更容易看清。

### 3.4 DELETE

```
DELETE(H, x):
  DECREASE-KEY(H, x, -∞)            // 用 int64 最小值即可
  EXTRACT-MIN(H)
```

你的 Dijkstra 不需要它；报告里写复杂度时提一句 O(log n) 摊还即可。

### 3.5 和 adaptor 方法的映射

你只需要实现 5 个私有方法：

| `BaseHeapAdaptor` 调用 | 你要实现 | 对应算法 |
| --- | --- | --- |
| 构造 `Base(n)` | `explicit FibonacciHeap(Vertex n)` | 分配 `stamp_`、节点数组 |
| `clear()` | `clear_impl()` | 清空根表/`min_`/`size_`（O(1)，见 §5.7） |
| `empty()` | `bool empty_impl() const` | `min_ == kNull` |
| `pop()` | `Entry pop_impl()` | EXTRACT-MIN |
| `push_or_decrease(k, v)` | `insert_impl(k, v)` / `decrease_impl(v, k)` | INSERT / DECREASE-KEY |

---

## 4. 摊还分析

这一节是报告里"分析"部分的骨架，也解释了为什么每个操作要那样设计。

### 4.1 势函数

对堆 H 定义势能

```
Φ(H) = t(H) + 2·m(H)
```

- `t(H)` = 根表里的树数（根节点数）；
- `m(H)` = 被 mark 的节点数。

直观理解：插入时每多一棵树，就"存"了 1 单位势能，足够支付将来把它 link 掉的工作；
每个 mark 存 2 单位势能，足够支付将它剪到根表（+1 根）并清掉 mark（−2 mark）的工作。

对操作序列，摊还代价 = 实际代价 + ΔΦ。

### 4.2 逐操作

**插入（INSERT）**：实际 O(1)，根数 +1 → ΔΦ = +1，
摊还 = O(1) + 1 = **O(1)**。

**MINIMUM / UNION**：ΔΦ = 0，**O(1)**。

**EXTRACT-MIN**：设 z 是 min，孩子数 c ≤ D(n)，合并前根数 t + c。

- 实际代价：搬孩子 O(c)、遍历根表 O(t + c)、consolidate 的 link 次数
  ≤ t + c（每 link 一次根数 −1）、扫描 A 数组 O(D(n))；
  总 O(t + D(n))（把 c 吸收进 D）。
- 势能变化：合并后根数 ≤ D(n) + 1；z 的孩子里原本 marked 的节点变成根，
  mark 被清掉，m 只会减少（记清掉的 mark 数为 m' ≥ 0）。所以
  `ΔΦ ≤ (D(n) + 1 − t) − 2m' ≤ D(n) + 1 − t`。
- 摊还 = O(t + D(n)) + (D(n) + 1 − t) = **O(D(n)) = O(log n)**。

**DECREASE-KEY**：设发生了 c 次级联剪枝（再算上对 x 自己的那次 cut，共 1+c 次）。
实际代价 O(1 + c)。

- 每次 cut 都让根数 +1，且被剪的节点若是 marked 则 m −1：
  - 对 x 的第一次 cut：若 x 原来 marked，ΔΦ 贡献 −1；若 unmarked，+1；
  - 每一层级联 cut：被剪的 y 必然 marked（否则会停在 mark 上），
    贡献 +1（根）+ (−2)（mark）= −1；
  - 级联结束处最多把一个节点从 unmarked 变成 marked：+2。
- 合计 `ΔΦ ≤ 3 − c`。
- 摊还 = O(1 + c) + (3 − c) = **O(1)**。

### 4.3 度上界引理（为什么 D(n) = O(log n)）

**引理**：度数为 k 的节点 x，其子树大小 `size(x) ≥ F_{k+2}`
（F 为 Fibonacci 数：F₀=0, F₁=1）。

证明思路（对 k 归纳）：设 x 的孩子按 link 到 x 的先后顺序为 y₁,…,y_k。
y_i 被链接时，x 的度数恰好是 i−1，且 link 只发生在度数相同的根之间，
所以 y_i 当时的度数也是 i−1。此后 y_i 最多失去一个孩子（mark 机制），
故它现在的度数 ≥ i−2。于是：

```
size(x) ≥ 1 + size(y₁) + Σ_{i=2..k} size(y_i)
        ≥ 1 + F₂ + Σ_{i=2..k} F_i          (归纳假设)
        = 1 + 1 + (F_{k+2} − 2)
        = F_{k+2}
```

又 Fibonacci 数满足 `F_{k+2} ≥ φ^k`，所以 `n ≥ size(x) ≥ φ^k`，即

```
k ≤ log_φ n = log₂ n / log₂ φ ≈ 1.4404 · log₂ n
```

n ≤ 2³² 时最大度数 ≤ 46。**因此 consolidate 的 A 数组取 64 就绝对安全**
（写代码时加 `assert(d < 64)`，一旦触发说明结构被破坏了）。

### 4.4 汇总

| 操作 | 实际最坏 | 摊还 |
| --- | --- | --- |
| INSERT | O(1) | O(1) |
| MINIMUM | O(1) | O(1) |
| UNION | O(1) | O(1) |
| EXTRACT-MIN | O(n) | O(log n) |
| DECREASE-KEY | O(n) | O(1) |
| DELETE | O(n) | O(log n) |

Dijkstra（懒插入 + decrease-key）：`O(n + m + n log n) = O(E + V log V)`。

---

## 5. 面向 proj2 的 C++ 实现设计

### 5.1 关键简化：节点索引 = 顶点号

你的 `BaseHeapAdaptor` 有一个很关键的设计（timestamp optimization）：

- `stamp_[v] != epoch_` 时调用 `insert_impl`，然后置 `stamp_[v] = epoch_`；
- `pop()` 之后 `stamp_[v] = 0`；
- `clear()` 只把 epoch 加一。

这意味着：**在一个 epoch（一次 Dijkstra）内，同一顶点在堆里最多只有一个活跃条目**。
而 Dijkstra 的非负权保证"顶点出堆后不会再被松弛"（已确定最短距离），
所以一次运行里每个顶点至多入堆一次。

于是可以做一个非常舒服的简化：

> **节点数组按顶点编号索引：`nodes_[v]` 就是顶点 v 的节点。**

#### handle 到底需不需要？——需要，但你的设计里它是"隐式"的

先澄清概念：**handle（迭代器 / 节点定位器）是 Fib 堆 `decrease-key` 的必需品**。
`decrease-key` 必须直接拿到目标节点去改 key、必要时 cut，不能做搜索。
Fib 堆里节点在 cut/link/consolidate 时会在根表和孩子表之间移动，
但**节点对象本身从不重建、地址稳定**，所以 handle 应该存"节点身份"，
而不是"它在哪个链表里"。

三种存法：

| 方案 | handle 是什么 | 额外存储 |
| --- | --- | --- |
| pb_ds `point_iterator` | 迭代器 / 节点指针 | `handle_[v]`：8 B/点 |
| 通用节点池 | 池下标（`uint32_t`） | `handle_[v]`：4 B/点 |
| **顶点索引节点（本方案）** | **顶点号 v 本身**（= `nodes_` 下标） | 不需要 |

之所以"不需要"，不是省略了 handle，而是 **handle 恰好等于 v**：
`BaseHeapAdaptor::decrease_impl(Vertex v, Key dist)` 的参数里已经带了 v，
而节点就是 `nodes_[v]`，所以 decrease-key 是 O(1) 直接定位：

```text
insert_impl(dist, v)  →  nodes_[v] 初始化为新节点
decrease_impl(v, d)   →  直接操作 nodes_[v]
pop_impl()            →  返回 {key, z}，z 既是节点下标也是顶点号
```

这个"恰好"成立的前提是：**同一时刻每个顶点至多一个活跃节点**。
这一点由 Base 的 stamp 机制保证：`insert_impl` 只在 `stamp_[v] != epoch_`
（v 当前不在堆里）时被调用；pop 后 stamp 清零，旧节点早已从堆中移除。
所以即使不看 Dijkstra 的语义，仅凭 adaptor 契约，顶点号就是合法的 handle。

> ⚠️ 注意区分：`stamp_` 只是"在不在堆里"的标记，**不记录节点位置**。
> 位置信息来自"节点数组下标 = 顶点号"这个设计，而不是来自 stamp。

两种实现路线对比：

| 方案 | 插入/查找 | 内存 | 适用 |
| --- | --- | --- | --- |
| **顶点索引节点数组**（推荐） | O(1)，直接 `nodes_[v]` | 32 B × n，一次性 | 路网、强连通图（大部分顶点被访问） |
| 通用节点池 + `handle_[v]` | O(1)，多一次间接寻址 | 32 B × 实际插入数 + 4 B × n | 只访问很小一部分顶点时 |

在 DIMACS 路网（强连通，几乎所有顶点都可达）上，两者内存差不多，
但顶点索引方案少一层间接、少维护一个数组，代码也简单得多，**先用它**。
如果以后评测发现大部分顶点不可达，或者想把这个 Fib 堆当通用容器复用，
再走"节点池 + `handle_`"路线（见 §6.1 末尾的可选架构 B）。

### 5.2 存储布局与 NIL 约定

```cpp
struct Node {
  Key key{};                        // 8 B
  uint32_t parent{kNull};           // 4 B
  uint32_t child{kNull};            // 4 B
  uint32_t left{kNull};             // 4 B
  uint32_t right{kNull};            // 4 B
  uint16_t degree{};                // 2 B（最大 46，uint8 也够，uint16 更稳）
  bool mark{};                      // 1 B
};                                  // 共 32 B（含 padding）
```

约定：

- `kNull = UINT32_MAX` 作为 NIL。**不要用 0**：如果顶点编号是 0-based，
  0 是合法顶点号；用 `UINT32_MAX` 就完全没歧义。
- `nodes_` 大小 `n + 1`（`n` 是顶点数）。这样 0-based（0..n−1）和 1-based（1..n）
  都能直接用，代价只是多一个节点槽位。
- `stamp_` 是基类私有的，大小 n+1，你不需要也不应该去动它。
- 所有链接都存 **索引**（`uint32_t`），不要存 `Node*`：调试友好、可整体 dump、
  并且强制你每次访问都从 `nodes_` 走（缓存行为一致）。

**顶点编号要在整个新流水线里统一**：`csr-graph.hpp`、`general-dijkstra.hpp`、
`fib-heap.hpp`、DIMACS loader、benchmark 全部约定一致（建议 0-based，
DIMACS 读入时减 1；或者沿用旧代码的 1-based）。这是接入时第一个要拍板的事，
一旦混用会非常难查。

### 5.3 双向循环链表原语

所有链表都循环、无哨兵。建议先写三个小函数（`h` 是链表里某个节点）：

```cpp
// 把单点 x 插到 h 的右边
void splice_after(uint32_t h, uint32_t x) {
  uint32_t r = nodes_[h].right;
  nodes_[x].left = h;   nodes_[x].right = r;
  nodes_[h].right = x;  nodes_[r].left = x;
}

// 把 x 从它所在的循环表里摘掉（无论表里剩几个元素）
// 注意：若 x 是唯一元素，调用方要自己把表头指针置 NIL。
void unlink_single(uint32_t x) {
  uint32_t l = nodes_[x].left, r = nodes_[x].right;
  nodes_[l].right = r;
  nodes_[r].left = l;
}

// 把环 B（bFirst..bLast）接在 a 后面：用于根表拼接 / 孩子环搬到根表
void splice_ring_after(uint32_t a, uint32_t bFirst) {
  uint32_t aNext = nodes_[a].right;
  uint32_t bLast = nodes_[bFirst].left;
  nodes_[a].right = bFirst;   nodes_[bFirst].left = a;
  nodes_[bLast].right = aNext; nodes_[aNext].left = bLast;
}
```

注意 `unlink_single` 对唯一元素也成立（`l == r == x` 时自赋值无害），
但要把"表头指针"（如 `min_`、`parent.child`）的更新交给调用方。

### 5.4 EXTRACT-MIN 的干净写法

CLRS 的伪代码把"孩子搬到根表 + 删除 z"揉在一起，实现时容易把指针写乱。
推荐按下面的顺序（已在 §3.2 给了伪代码，这里只强调易错点）：

1. **先保存** `z.key`（返回值要用，后面清理 z 时不要把它改了）；
2. 若 z 有孩子：遍历孩子环，把每个孩子的 `parent = kNull`、`mark = false`；
3. 把**整圈孩子环**替换根表里的 z（`splice_ring_after`），而不是一个个插；
4. 若 z 没有孩子：按普通双向链表删除，注意根表只剩 z 一个的情况；
5. 删完后把 `nodes_[z]` 的链接字段全部置 NIL、degree=0、mark=false
   （为将来复用同一槽位做准备）；
6. 若根表非空再 `consolidate()`，`min_` 由 consolidate 重新计算；
7. `--size_`，返回 `{key, z}`。

### 5.5 CONSOLIDATE：必须用快照，别照抄 CLRS 的 "for each"

这是 Fibonacci 堆实现里**最经典的坑**：

CLRS 伪代码写的是

```
for each node w in the root list of H
    x = w
    d = x.degree
    while A[d] != NIL
        ...
```

如果你把它直译成"保存 next 指针、遍历根表、边遍历边 link"，当出现**相等 key** 时
会出事：设 `start` 是根表里 key 最小的根，某个与它同 key 的根 x 在 consolidate
中把 `start` link 成了自己的孩子（`key` 相等时 `x.key > y.key` 为假，所以 x 留下当父），
`start` 就从根表里消失了——而你的遍历终止条件可能还是"回到 start"，
于是**死循环或漏处理节点**。Dijkstra 的整数距离里相同 key 非常常见（路网尤其如此），
这个 bug 一定会踩到。

正确做法（推荐）：

```cpp
void consolidate() {
  // 1) 快照当前根表
  roots_.clear();
  uint32_t w = min_;
  do {
    roots_.push_back(w);
    w = nodes_[w].right;
  } while (w != min_);

  // 2) 按快照逐个处理（roots_ 不会因为 link 而改变，安全）
  A_.fill(kNull);
  for (uint32_t r : roots_) {
    uint32_t x = r;
    uint32_t d = nodes_[x].degree;
    while (A_[d] != kNull) {
      uint32_t y = A_[d];
      if (nodes_[y].key < nodes_[x].key) std::swap(x, y);
      link(y, x);          // y 从根表摘除
      A_[d] = kNull;
      ++d;
      assert(d < kMaxDegree);
    }
    A_[d] = x;
  }

  // 3) 由 A_ 重建根表，同时重选 min_
  min_ = kNull;
  for (uint32_t u : A_) {
    if (u == kNull) continue;
    nodes_[u].left = nodes_[u].right = u;      // 先成单点环
    if (min_ == kNull) { min_ = u; continue; }
    splice_after(min_, u);                     // 拼到 min_ 右边
    if (nodes_[u].key < nodes_[min_].key) min_ = u;
  }
}
```

为什么快照版本一定正确：

- `roots_` 中每个节点在被处理时一定是根：link 只会发生在"已经被处理过"的节点
  （A 桶里的节点）和当前节点 x 之间，未处理的快照节点不会被 link；
- 快照保证每个原根恰好被处理一次，不存在"遍历指针被改掉"的问题；
- 最后 A_ 里恰好是剩下的所有根，用它们重建根表保证没有节点丢失。

`roots_` 用成员 `std::vector<uint32_t>` 复用（每次 `clear()` 后 push_back，
capacity 一直保留），避免每次 extract-min 分配内存。
`A_` 用成员 `std::array<uint32_t, kMaxDegree>`，每次 `fill(kNull)`（64 次写，
比动态分配便宜得多；若要进一步优化，可以记录 touched 槽位只清这些）。

### 5.6 级联剪切

按 §3.3 的伪代码写，注意三点：

1. **cut 之前先存父指针**（cut 会把 `parent` 置 NIL）；
2. **cut 之后 `mark` 必须清零**（节点成了根）；
3. **级联循环里不会把根 mark 掉**：被 cut 的节点的父一定非根
   （它至少有一个孩子），所以"如果 unmarked 就 mark 并停"这一步
   永远作用在非根节点上。这一点可以在 debug 里断言。

### 5.7 clear()、epoch 与内存复用

`Base::clear()` 做的事：`++epoch_`，然后调用你的 `clear_impl()`。
所以你的 `clear_impl()` 应该**只做 O(1) 的工作**：

```cpp
void clear_impl() {
  min_ = kNull;
  size_ = 0;
  // 不需要清 nodes_：insert_impl 会整体重置被复用的节点槽位；
  // 不需要清 roots_ / A_：它们在下一次使用时（consolidate）会被重写；
  // 不需要清 handle / stamp：架构 A 里 handle 就是顶点号，且 stamp 由基类 epoch 管理
}
```

为什么旧数据不会漏读？

- `decrease_impl(v, ...)` 只会在 `stamp_[v] == epoch_` 时被调用，也就是 v 在本轮
  已经被 `insert_impl` 过；此时 `nodes_[v]` 的所有字段都是本轮重新写过的。
- `pop_impl()` 只会访问根表/孩子表可达的节点，这些节点都是本轮插入的。
- 上一轮遗留的 `nodes_[v]` 内容可能还在，但不会被读到；下一轮插入 v 时
  `insert_impl` 会覆盖全部 7 个字段（**务必把 left/right/parent/child/degree/mark
  全部重置**，否则残留指针会把两个 epoch 的链表接在一起，出现极难查的 bug）。

### 5.8 常见 bug 清单（按踩坑概率排序）

1. **consolidate 边遍历边 link**（§5.5），相等 key 时死循环或丢节点。
2. **忘了更新 `min_`**：insert 时、extract-min 后、decrease 根之后、cut 之后。
3. **把 min 的孩子搬到根表时忘了 `parent = NIL` 或 `mark = false`**。
4. **A 数组越界**：`degree` 上界算错 / 数组太小 / 链表坏了导致 degree 异常增长。
   用 64 + debug 断言。
5. **删除链表元素时没处理"唯一元素"**，或没更新 `parent.child`
   （被删的是 child 表头时）。
6. **复用节点槽位时没有重置全部字段**（顶点索引方案的必修课，见 §5.7）。
7. **把根 mark 成 true**（根不变量被破坏），通常发生在 cascading-cut 边界写错。
8. **decrease 时没判断"新 key 是否真的小于父 key"** 就盲目 cut；
   Dijkstra 保证严格更小，但独立单测里可能出现非法输入，加断言能早点发现。
9. **空堆上 pop**（调用方先 `empty()` 检查；自己加 `assert(min_ != kNull)`）。
10. **顶点号/NIL 混用**：0-based 顶点号与 `kNull=0` 冲突，或 1-based 数组大小差 1。
11. **pop 后清理 z 时把返回值用的 key 改了**（先保存 key）。
12. **在 `pop_impl` 里试图手动清 stamp**（做不到，也不需要）。
13. **把 `Entry` 的字段顺序搞反**（`std::pair<Key, Vertex>`，不是 (v, d)）。
14. **A 数组每次用前没清空**，残留上一轮的节点索引 → 把旧节点当本轮根 link 进去，
    典型表现是随机崩溃/死循环。

### 5.9 性能优化清单（先正确，再快）

- 节点 32 B，`nodes_` 一次性分配（构造时 `resize(n+1)`），**不要每次 Dijkstra new 对象**；
  用 `clear()` 复用。
- `roots_` 复用成员 vector，`reserve` 一次；`A_` 用 `std::array`。
- `link`/`cut`/`splice` 全部内联、只用索引；避免 `std::function`、异常、虚函数。
- 比较只比 key（不要构造 `Entry` 再比较）；相等 key 的 tie-break 可选
  （例如比 vertex 号），对 Dijkstra 结果无影响。
- consolidate 的 `A_.fill` 若成为热点，改成记录 touched 槽位。
- 不要在计时路径里做 debug 断言（Release 下 `NDEBUG` 自动去掉；
  单测/验证时用 Debug 构建跑）。
- 若想更进一步压内存：`mark` 可以塞进 `degree` 的高位（degree ≤ 46 < 32768），
  但优先保证可读性，报告里写明取舍即可。

---

## 6. 接入 proj2 的步骤

### 6.0 先拍板三个全局约定

1. **顶点编号**：0-based（0..n−1）还是 1-based（1..n）？
   建议 0-based（C++/CSR 自然），DIMACS 读入时减 1；`kNull = UINT32_MAX`，
   `nodes_` 大小 n+1，两种编号都能容纳。
2. **距离类型**：`int64_t`，`INF = INT64_MAX / 4`（避免加法溢出）。
3. **图表示**：CSR（`csr-graph.hpp`），所有堆共用同一份图，保证公平。

### 6.1 新增 `pq-test/fib-heap.hpp`

骨架（声明，方法体你自己填；这个骨架已编译验证）：

```cpp
//
// Created by Renatus Madrigal on 2026/10/26.
//

#pragma once
#ifndef FIB_HEAP_HPP
#define FIB_HEAP_HPP

#include "heap-adaptor.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace dijkstra_bench {

// 注意：继承 BaseHeapAdaptor<FibonacciHeap, true>，true 表示支持 decrease-key
class FibonacciHeap final : public BaseHeapAdaptor<FibonacciHeap, true> {
public:
  using Base = BaseHeapAdaptor<FibonacciHeap, true>;
  using Key = typename Base::Key;
  using Vertex = typename Base::Vertex;
  using Entry = typename Base::Entry;

  // n = 顶点数；顶点编号 0..n-1（1-based 时传 n，保证 v <= n）
  explicit FibonacciHeap(Vertex n) : Base(n), nodes_(static_cast<size_t>(n) + 1) {}

private:
  friend struct BaseHeapAdaptor<FibonacciHeap, true>;

  static constexpr uint32_t kNull = UINT32_MAX;
  static constexpr size_t kMaxDegree = 64;  // n <= 2^32 时 D(n) <= 46

  struct Node {
    Key key{};
    uint32_t parent{kNull};
    uint32_t child{kNull};
    uint32_t left{kNull};
    uint32_t right{kNull};
    uint16_t degree{};
    bool mark{};
  };

  // ---------------- BaseHeapAdaptor 契约：必须实现 ----------------
  void clear_impl();
  bool empty_impl() const;
  Entry pop_impl();
  void insert_impl(Key dist, Vertex v);
  void decrease_impl(Vertex v, Key dist);

  // ---------------- 内部工具：自己实现 ----------------
  void splice_after(uint32_t h, uint32_t x);
  void unlink_single(uint32_t x);
  void add_root(uint32_t x);
  void link(uint32_t y, uint32_t x);
  void cut(uint32_t x);
  void cascading_cut(uint32_t y);
  void consolidate();

  // 可选：debug 下做 O(size) 的结构检查
  // void validate() const;

  uint32_t min_{kNull};
  uint32_t size_{0};
  std::vector<Node> nodes_;              // 节点索引 == 顶点编号
  std::vector<uint32_t> roots_;          // consolidate 的快照缓冲（复用）
  std::array<uint32_t, kMaxDegree> A_{}; // consolidate 的度数桶
};

} // namespace dijkstra_bench

#endif // FIB_HEAP_HPP
```

如果你不习惯成员名带下划线，可以改成你自己的风格；关键是**类声明结构和
`friend struct BaseHeapAdaptor<FibonacciHeap, true>;` 这一行不能少**——
否则基类访问不到你的私有 `_impl`（我用编译探针验证过这一点）。

#### 可选架构 B：通用 Fib 堆 + 薄 adaptor（想显式用 handle 时）

如果你更希望 Fibonacci 堆本身是一个可复用的通用容器（这也更贴近
"实现一个 Fibonacci 堆"的报告叙述），可以拆成两层：

```cpp
// fib-heap.hpp —— 通用容器：insert 返回 handle（节点池下标）
class FibonacciHeap {
public:
  using Handle = uint32_t;            // 节点池下标；NIL 用 UINT32_MAX
  explicit FibonacciHeap(size_t reserve);
  Handle insert(Key k, Vertex v);     // 返回 handle
  Entry extract_min();
  void decrease(Handle h, Key k);
  bool empty() const;
  void clear();
};

// fib-heap-adaptor.hpp —— 只做 handle 映射 + 适配 BaseHeapAdaptor
class FibHeapAdaptor final : public BaseHeapAdaptor<FibHeapAdaptor, true> {
  // ...
  void insert_impl(Key d, Vertex v) { handle_[v] = heap_.insert(d, v); }
  void decrease_impl(Vertex v, Key d) { heap_.decrease(handle_[v], d); }
  // ...
  FibonacciHeap heap_;
  std::vector<FibonacciHeap::Handle> handle_;
};
```

这就是 pb_ds adaptor 的形态：`insert` 返回的 handle 必须存进 `handle_[v]`，
`decrease` 时再查出来。它比架构 A 多一层间接和 4 B/点的 handle 数组，
但堆可以独立复用、单测更直接、报告分层更清晰。
**两条路线都正确，先选一条走通，不要同时维护两套。**

### 6.2 接口契约表（Base 对你的保证 / 你的义务）

| Base 的行为 | 你的义务 |
| --- | --- |
| `Base(n)` 分配 `stamp_`（n+1 个 `uint32_t`） | 构造函数里把 `n` 传对：`Base(n)` |
| `clear()` 先 `++epoch_`，再调 `clear_impl()` | `clear_impl()` 只清堆结构，**O(1)** |
| `pop()` 调 `pop_impl()` 后清 `stamp_[v]` | 返回 `{key, vertex}`；只在非空时调用 |
| `push_or_decrease(k,v)`：`stamp_[v] != epoch_` → `insert_impl(k,v)` | 建立节点、插入、更新 `min_` |
| 同 epoch 内再次 `push_or_decrease(k,v)` → `decrease_impl(v,k)` | 此时 v **一定在堆中**；`k` 一定更小（Dijkstra 保证） |
| `clear()` 后 `stamp_` 全失效 | 不需要清任何显式 handle；本方案里 handle 就是顶点号，天然有效 |
| `Heap::decreaseable` = true | `general-dijkstra.hpp` 里可以 `if constexpr` 分流 |

### 6.3 单测：`pq-test/fib-heap-test.cpp`（gtest）

这是**最先要做、收益最大**的一步。测试分三层：

**① 模型对拍（不涉及图）**

用 `std::multiset<Entry>` 当参照物，随机操作：

```
维护：active[v]（v 是否在堆里）、cur[v]（当前 key）
随机选：
  a) 插入一个不在堆里的 v，key 随机
  b) decrease 一个在堆里的 v（新 key 更小）
  c) pop（若堆非空）→ 与 model 的最小 key 比较（只比 key，别比 vertex，
     因为相等 key 的先后顺序不要求一致）
  d) clear() → model.clear()，所有 active 置 false
每步后检查 heap.empty() == model.empty()；用固定 seed（mt19937(12345)），
失败可以直接复现。
```

测试规模：n=1000、操作 2×10⁵ 一轮；n=10⁴、操作 10⁶ 再一轮。
Decrease 的 key 只减不增（这是 adaptor 的契约），不要喂非法序列。

**② 结构不变量（debug 专用）**

在 `fib-heap.hpp` 里加一个 `#ifndef NDEBUG` 的 `validate()`（或测试里的 friend）：

- 从 `min_` 走一圈根表：验证循环闭合、`parent == kNull`、`mark == false`；
- 对根表每个根 DFS/BFS：孩子数 == degree、父亲正确、堆序正确；
- 统计节点数 == 你期望的 `size_`，`min_` 的 key 等于全局最小 key；
- 检查 mark 的合法性（非根才可能为 true）。

对拍测试每一步后调用 `validate()`（n 小的时候），或者每 1000 步一次。

**③ 与 `StdHeapAdaptor` 的行为对比**

同一组 `push_or_decrease`/`pop` 调用序列，分别喂给 `FibonacciHeap` 和
`StdHeapAdaptor`（后者不支持 decrease，语义上等价于懒删除：每次 decrease 就是
再 push 一份）。对比时注意过滤 std 的过期条目：只有
`du == cur[v]` 且 v 尚未被"定稿"的 pop 才是有效输出；两边的**有效输出 key 序列**
必须完全一致（相等 key 时 vertex 的先后不要求一致）。这一步验证你和现有 adaptor
的语义对齐，避免"堆本身没错、但接入语义错了"。

### 6.4 实现 `general-dijkstra.hpp`

骨架（你已经有 stub，下面是接入要点）：

```cpp
template <typename Graph, typename Heap>
void dijkstra(const Graph &g, uint32_t s, std::vector<int64_t> &dis, Heap &heap) {
  constexpr int64_t kInf = std::numeric_limits<int64_t>::max() / 4;
  std::fill(dis.begin(), dis.end(), kInf);
  heap.clear();                       // 每次运行复用同一个堆对象

  dis[s] = 0;
  heap.push_or_decrease(0, s);

  while (!heap.empty()) {
    auto [du, u] = heap.pop();
    // 懒删除堆（std::priority_queue）需要跳过过期条目；
    // decreaseable 的堆不会出现 du != dis[u]。
    if constexpr (!Heap::decreaseable) {
      if (du != dis[u]) continue;
    }
    for (const auto &e : g[u]) {
      int64_t nd = dis[u] + e.w;
      if (nd < dis[e.v]) {
        dis[e.v] = nd;
        heap.push_or_decrease(nd, e.v);
      }
    }
  }
}
```

注意事项：

- `heap.clear()` 放在 `fill` 之后、第一次 push 之前；调用方负责
  `Heap heap(n)`，n = 顶点数 = `dis.size()`。
- 不要在循环里用 `dis[u]` 之外的值做松弛；decreaseable 时 `du == dis[u]`。
- `Graph::operator[]` 返回的是 `std::span<const Edge>`，范围 for 直接用。
- 这个函数对 `Graph` 的唯一要求就是 `g[u]` 可范围遍历、`Edge` 有 `.v`/`.w`；
  单测可以直接传 `std::vector<std::vector<EdgeStub>>`，不依赖 CSR builder。

### 6.5 补 `csr-graph.hpp`

目前只有 `operator[]`，需要补：

```cpp
class Graph {
public:
  struct Edge { uint32_t v; int32_t w; };

  // 1) builder：从边表构建（无向图记得存两条）
  static Graph from_edges(uint32_t n,
                          const std::vector<std::tuple<uint32_t, uint32_t, int32_t>> &edges,
                          bool undirected);

  // 2) 访问
  std::span<const Edge> operator[](uint32_t u) const;
  uint32_t size() const { return static_cast<uint32_t>(offset_.size()) - 1; }

  // 3) DIMACS loader（.gr 文件）
  static Graph load_dimacs_gr(const std::filesystem::path &path);
  // .co 是坐标，Dijkstra 不需要；如果以后做 A* 再加
};
```

DIMACS `.gr` 格式（9th DIMACS Challenge）：

```
c 注释行
p sp <n> <m>
a <u> <v> <w>          # 1-based，有向弧（无向路网会给出两个方向）
```

实现要点：

- 57M 行级别，**不要用 `operator>>`**，用 `fread` + 手写整数解析；
- 先用 `p sp n m` 预分配，再读 `a` 行；有向图按 `m` 条存；
- 读入时把 1-based 转成你约定的编号（建议 0-based）。

### 6.6 新 benchmark 入口与 CMake

建议**新开一个文件**（例如 `pq-test/dij-bench-v2.cpp`），保留旧的
`dij-bench.cpp` 以维持历史结果可复现；新文件里所有堆共用同一套图/查询/计时逻辑：

```cpp
template <typename Heap>
void run_dijkstra_bench(benchmark::State &state) {
  const auto n = static_cast<uint32_t>(state.range(0));
  const auto m = static_cast<uint32_t>(state.range(1));
  // 图只构建一次（在 state 循环外），所有 iteration 复用
  const Graph g = make_random_graph(n, m, /*seed=*/42);
  std::vector<int64_t> dis(n);
  Heap heap(n);
  uint64_t checksum = 0;
  for (auto _ : state) {
    dijkstra(g, /*s=*/0, dis, heap);
    checksum ^= static_cast<uint64_t>(dis[dis.size() - 1]);  // 防优化
  }
  benchmark::DoNotOptimize(checksum);
  state.SetComplexityN(n);
}

#define BENCH_HEAP(tag, HeapType)                                            \
  static void bench_##tag(benchmark::State &state) {                          \
    run_dijkstra_bench<HeapType>(state);                                      \
  }                                                                           \
  BENCHMARK(bench_##tag)->Apply(dijkstraArguments)->Complexity(benchmark::oNLogN)

BENCH_HEAP(fib, FibonacciHeap);
BENCH_HEAP(std, StdHeapAdaptor);
#ifdef DIJKSTRA_BENCH_HAS_PBDS
BENCH_HEAP(pairing, PbdsPairingHeapAdaptor);
BENCH_HEAP(thin, PbdsThinHeapAdaptor);
#endif
```

`pq-test/CMakeLists.txt` 里加：

```cmake
# 旧的保持不动
pbds_add_benchmark(dij-bench dij-bench.cpp)
pbds_add_benchmark(dij-bench-hack-spfa dij-bench-hack-spfa.cpp)

# 新架构
pbds_add_benchmark(dij-bench-v2 dij-bench-v2.cpp)

# 单测（gtest）
add_executable(fib-heap-test fib-heap-test.cpp)
target_link_libraries(fib-heap-test PRIVATE GTest::gtest_main)
if(MSVC)
  target_compile_options(fib-heap-test PRIVATE /W4 /utf-8)
else()
  target_compile_options(fib-heap-test PRIVATE -Wall -Wextra)
endif()
add_test(NAME fib-heap-test COMMAND fib-heap-test)
```

CMake 注意点：

- `GTest::gtest_main` 别名由 vendored googletest 提供（`enable_testing()` 已在
  `proj2/CMakeLists.txt` 里调用过，所以 `add_test` 有效）。
- **`pq-test` 子目录只在 `PBDS_BENCHMARKS=ON`（即 GCC）时才会被 add**。
  如果你希望新架构在非 GCC 编译器上也能构建（例如只用 std heap + Fib 堆），
  后续可以把 `pq-test` 整体从 `if(PBDS_BENCHMARKS)` 里挪出来，
  只把 pb_ds 相关 target 留在条件里；或者新开一个 `dijkstra-bench/` 子目录
  无条件 add。这是重构后期的事，先不阻塞。
- `heap-adaptor.hpp` 里 pb_ds 的 include 由 `DIJKSTRA_BENCH_HAS_PBDS` 宏守卫，
  新文件不需要自己判断编译器。

### 6.7 公平性与计时

- **同一张图、同一个 seed、同一个源点**跑所有堆；旧
  `graph-generator.hpp` 用 `random_device` 生成图，每个堆看到的图不一样，
  新 harness 要避免。
- 图构建、`dis` 初始化（`std::fill`）都计入 Dijkstra 计时的话，所有堆都计入，
  保持公平；图构建本身放在 `for (auto _ : state)` 循环外。
- 用 `benchmark::DoNotOptimize` / `benchmark::ClobberMemory` 防止整个循环被优化掉。
- Release 构建（`-O2` 或 `-O3`）；不要用带 `assert` 的 Debug 跑最终数字。
- 用 `--benchmark_out=result.json` 输出结构化结果，后面画图。
- 想报告 "操作次数"（insert/decrease/pop 次数），在 heap 里加计数器，
  但只在**单独的计数构建**里加，别污染计时路径。

### 6.8 DIMACS 数据与 1000 查询

- 数据页面：<http://www.diag.uniroma1.it/challenge9/download.shtml>
  （报告里只放下载链接，不要上传数据，这是 PROJECT.md 的要求）。
- 关注文件：`USA-road-d.<REGION>.gr`（图，有向弧）、`.co`（坐标，可选）、
  `.ss`（单源查询集）、`.p2p`（点对查询集）。
- `.ss` / `.p2p` 的文件头是 `p aux sp ss <k>` / `p aux sp p2p <k>`，
  **查询条数以文件头为准**（常见是每文件 100 个源 / 1000 对；
  不同 region 可能不同，解析时按头里的 k 循环读 `s`/`q` 行）。
- 规模从 `<REGION>` = NY/BAY/COL（几十万点）一直到 USA（约 2.4×10⁷ 点、
  5.8×10⁷ 弧）。全图 × 1000 对查询会非常慢，建议：
  - 小/中图（NY、BAY、COL、FLA、NW、NE）跑完整 1000 对；
  - 大图（E、W、CTR、USA）跑少一些查询（例如 100 对）并报告单次平均；
  - 或者分布式/多进程分摊（同一台机器上跑不同源点，结果按图汇总）。
- 内存估算：Fib 堆 `nodes_` 32 B × n（USA 约 766 MB），CSR 约
  `4(n+1) + 8m` ≈ 560 MB，`dis` 8n ≈ 192 MB。全图跑之前先算清楚内存。
- 用生成的随机图（旧 benchmark 的 `dijkstraArguments` 那套 n/m 序列）做
  "时间 vs 规模"的曲线；用 DIMACS 路网做"真实数据对比表"。

### 6.9 报告素材（对应评分点）

- 第 2 章：Fibonacci 堆结构 + 势能分析（§2–§4 的内容可以直接整理）；
- 实现章：顶点索引节点、consolidate 快照、clear/epoch 的设计取舍；
- 实验章：
  - 堆对比表（std binary / Fib / pairing / thin / segment tree...）；
  - 时间 vs n、时间 vs m 的图；
  - DIMACS 各图、各堆的平均单次查询时间；
  - 操作次数统计（insert/decrease/pop 比例）；
  - 分析："为什么 Fib 堆理论上界更好但实测常数大"、"decrease-key 的实际
    收益与图结构的关系"、"段树堆/radix heap 为什么在整数权路网上有优势"。
- 附录：代码结构、复现命令、数据来源链接、gtest 说明。

---

## 7. 测试与验收清单

按顺序做，前面的不过不要做后面的：

- [ ] `FibonacciHeap` 能编译（对着 `heap-adaptor.hpp`，无警告）。
- [ ] 单测 ① 模型对拍通过（随机 insert/decrease/pop/clear，固定 seed）。
- [ ] 单测 ② 结构不变量 `validate()` 在对拍过程中全程通过。
- [ ] 单测 ③ 与 `StdHeapAdaptor` 的 key 序列一致。
- [ ] `general-dijkstra.hpp` 用 `StdHeapAdaptor` 跑通，并与朴素
      `priority_queue` 参考实现对拍（随机图，权值 1..1000）。
- [ ] 换 `FibonacciHeap`，同一批随机图上 `dis` 数组完全一致。
- [ ] 10⁶ 次随机操作压测（Debug，`assert` 全开）无崩溃、无断言。
      如果想再加 `-D_GLIBCXX_DEBUG`，请单独写一个不链接 gtest 的
      压力测试 exe（`_GLIBCXX_DEBUG` 会改变标准库容器布局，和预编译的
      gtest 库 ABI 不兼容，混用会随机崩溃）。
- [ ] Release 下 benchmark 跑通，与 std heap 的校验和一致。
- [ ] DIMACS loader 能读 `.gr` 并抽查边数/顶点数；跑一个 region 的 1000 对。
- [ ] 生成报告用的时间表和图。
- [ ] 全项目 `-Wall -Wextra` 无警告；代码注释满足评分要求。

调试建议：

- 单测 target 用 Debug 配置（`assert` 全开）；benchmark target 用 Release；
- 如果环境支持，开 `-fsanitize=address,undefined` 跑单测；
- 崩溃时先怀疑 §5.8 清单里的前 6 条。

---

## 8. 参考资料

1. **CLRS《算法导论》** 第 19 章 *Fibonacci Heaps*（第 3 版章节号）：
   19.1 结构、19.2 可合并堆操作、19.3 减键与删除、19.4 最大度数上界。
   本文的伪代码与势能分析都以这一章为准。
2. **M. L. Fredman, R. E. Tarjan**, *Fibonacci Heaps and Their Uses in Improved
   Network Optimization Algorithms*, JACM 34(3), 1987.
   （Fib 堆的原始论文，Dijkstra / Prim 的 O(E + V log V) 结果出处。）
3. **9th DIMACS Implementation Challenge — Shortest Paths**：
   <http://www.diag.uniroma1.it/challenge9/>（格式说明与数据下载；
   报告里只放链接）。
4. 你课程里已有的堆材料：`courseware/ADS05BinomialQ_Stu(6).ppt`（二项队列）、
   `courseware/ADS04LeftistSkewH_Stu(8).ppt`（左倾/斜堆）——Fib 堆可以看作
   "二项队列 + 懒合并 + 剪切"的进一步放松，对照着看会更容易理解。
5. （可选）如果你想要一个工业级参照物：Boost.Heap 有
   `boost::heap::fibonacci_heap`，可以拿它当"行为参照"，**但不能用于提交**
   （项目要求自己实现）。当前 MSYS2 环境里没装 Boost，需要自己装或用 pb_ds
   的 thin heap 作为替代参照。

---

## 附：建议的推进顺序

1. 先写 `fib-heap.hpp` 的**无 decrease 版本**（把派生模板参数改成 `false`，
   只实现 `push_impl` + `pop_impl` + consolidate），接上 `general-dijkstra.hpp`
   和 `StdHeapAdaptor` 对拍——这一步只验证"根表 + consolidate + pop"的正确性，
   没有级联剪切的干扰。
2. 升级成 `true` 版本：加 `insert_impl` / `decrease_impl` / `cut` /
   `cascading_cut`，做模型对拍和不变量检查。
3. 接 benchmark，先跑随机图，确认和 std heap 的 dis 完全一致、性能量级合理。
4. 写 DIMACS loader，跑真实路网，出表出图。
5. 最后再考虑微优化（consolidate 的 A 清理、节点布局、mark 打包）。

> 记住：**先把正确性钉死，再谈性能**。Fib 堆的 bug 大多不会立刻崩，
> 而是在某个随机图上给出错误的 `dis`——所以模型对拍和 `validate()` 是必需品，
> 不是可选项。
