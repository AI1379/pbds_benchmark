proj2 报告底稿与绘图脚本

入口
  main.tex                主文档（XeLaTeX，ctexart）
  report-config.tex       匿名开关、底稿提示、作者与日期
  report.pdf              已编译预览
  sections/               四章正文、源码附录、参考文献、分工声明与签名
  data/                   本轮 CSV 快照及不含用户名/机器名的溯源信息
  figures/                脚本生成的 PDF 矢量图与 PNG
  tables/                 由 CSV 自动生成的 TeX 数值表

结构
  对应参考 Word 的 Introduction、Data Structure / Algorithm Specification、
  Testing Results、Analysis and Comments，随后为 Source Code、References、
  Author List、Declaration、Signatures。采用中文内容。
  默认匿名，未填入姓名、学号或签名。非匿名版本需在 report-config.tex 中切换。
  当前分工、签名及源码注释比例是明确的待完成项，不是已经满足的声明。

重建（在 proj2 根目录运行，Windows / WSL / Linux 均可）
  python -m pip install -r tools/requirements-plot.txt
  python tools/plot_benchmarks.py
  python reports/build_report.py

当前机器也可直接用已装好 matplotlib 的 WSL Python 生成图表：
  wsl -d archlinux -- python3 /mnt/d/Projects/ACTraining/ads-projects/proj2/tools/plot_benchmarks.py
  编译可以使用 Windows 或 WSL 中的 XeLaTeX。无需重新运行 benchmark。
  本机 Windows TeX 缺少 placeins 包，已使用 WSL 完整 TeX 环境生成预览：
  wsl -d archlinux -- python3 /mnt/d/Projects/ACTraining/ads-projects/proj2/reports/build_report.py

可独立运行的脚本
  python tools/plot_dimacs.py
  python tools/plot_complexity.py
  python tools/plot_random.py
  python tools/generate_report_tables.py
  脚本均接受 --data-dir 和 --output-dir。plot_benchmarks.py 的 --output-dir
  控制图片位置，数值表仍写到 reports/tables/，供当前报告直接引用。

依赖
  Python 3.10+，matplotlib 3.7+（其余数据处理仅用标准库）。
  TeX Live：XeLaTeX、ctex、Fandol、geometry、booktabs、tabularx、longtable、
  listings、enumitem、caption、placeins、fancyhdr、hyperref 等常用包。

数据口径
  随机图 171 组，复杂度 126 组，均重复 3 轮；NY/BAY 各 9 种实现，
  每项完整执行 1000 查询，一个批次。共 909 条原始测量，2151 行总 CSV。
  图中随机与复杂度采用 CPU 毫秒，误差棒为三轮样本标准差。
  路网采用平均每查询墙钟毫秒，没有重复批次方差。
  原始点对点输入仍执行完整单源搜索；不含目标提前终止。
  随机图按固定 n 分面，避免把同时变化的 n/m 阶梯当作复杂度曲线。
  PDF 图片用于报告；PNG 用于快速查看。图例采用英文以避免本地字体依赖。
  all-results.csv 的 aggregate / CV / RMS 行不当作独立测量。
  random-reachability.csv 是同一生成器、种子和源点的独立可达性诊断，
  由 tools/inspect_random_graphs.cpp 生成；不属于计时测量，也不替换原始结果。

正式提交前
  核对实际作者及贡献、课程允许的工具辅助声明、签名和源码注释比例。
  将 DraftNotes 关掉并选择课程要求的匿名或署名版本。
  依当期课程通知确定文件名；Word 中旧的年份示例不是本底稿的命名默认值。
  原始 .gr 路网大文件未打包进报告目录。
