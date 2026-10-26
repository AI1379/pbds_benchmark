# third_party — vendored 第三方依赖

本目录以源码快照（vendored）方式内嵌第三方库，不使用 submodule 或包管理器，
保证 `ads-proj2` 分支在离线环境下也能直接构建。

| 库 | 版本 | upstream commit | 许可证 | 目录 | 构建目标 |
| --- | --- | --- | --- | --- | --- |
| GoogleTest（googletest + googlemock） | v1.18.0 | `063de7e9578f82b369302001269680b4b1553359` | BSD-3-Clause | `googletest/` | `gtest`、`gtest_main`、`gmock`、`gmock_main` |
| Google Benchmark | v1.9.5 | `192ef10025eb2c4cdd392bc502f0c852196baa48` | Apache-2.0 | `benchmark/` | `benchmark`、`benchmark_main` |

源码来源：

- <https://github.com/google/googletest/releases/tag/v1.18.0>
- <https://github.com/google/benchmark/releases/tag/v1.9.5>

## 裁剪说明

为控制仓库体积，只保留构建库所必需的内容，删除了 upstream 中的测试、示例、
文档、CI 配置和 Bazel 构建文件。保留的部分：

- `googletest/`：`LICENSE`、`README.md`、`CMakeLists.txt`、
  `googletest/{CMakeLists.txt,cmake,include,src}`、`googlemock/{CMakeLists.txt,cmake,include,src}`
- `benchmark/`：`LICENSE`、`README.md`、`CMakeLists.txt`、`cmake/`、`include/`、`src/`

两个库在 upstream 中默认就不构建 tests/samples（`gtest_build_tests=OFF`、
`BENCHMARK_ENABLE_TESTING=OFF`），所以裁剪不影响正常构建。
后续如需运行 upstream 自带的测试或 Python 工具，请按下面的更新步骤重新拷贝对应目录。

## 构建接入

根目录 `CMakeLists.txt` 已通过 `add_subdirectory` 引入，并关闭了 vendored 项目的
测试/安装规则：

```cmake
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)
add_subdirectory(third_party/benchmark EXCLUDE_FROM_ALL)
```

- GoogleTest 要求 C++17（由 target 的 `cxx_std_17` 指定）
- Google Benchmark 在本仓库中固定用 C++17 构建
- 本仓库自身源码使用 C++20（与 `.vscode/tasks.json` 保持一致）

构建方式（在仓库根目录）：

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 更新方法

以 GoogleTest 为例（Benchmark 同理，替换仓库地址、tag 和目标目录）：

```bash
# 1. 在仓库外克隆指定 tag
git clone --depth 1 --branch v1.18.0 https://github.com/google/googletest.git /tmp/googletest
git -C /tmp/googletest rev-parse HEAD    # 记录新的 commit，填入上表

# 2. 清空旧目录，避免残留已删除的文件
rm -rf third_party/googletest
mkdir -p third_party/googletest/googletest third_party/googletest/googlemock

# 3. 拷贝裁剪后的子集
cp /tmp/googletest/{LICENSE,README.md,CMakeLists.txt} third_party/googletest/
cp /tmp/googletest/googletest/CMakeLists.txt third_party/googletest/googletest/
cp -r /tmp/googletest/googletest/{cmake,include,src} third_party/googletest/googletest/
cp /tmp/googletest/googlemock/CMakeLists.txt third_party/googletest/googlemock/
cp -r /tmp/googletest/googlemock/{cmake,include,src} third_party/googletest/googlemock/

# 4. 更新上表中的版本号和 commit，然后重新构建验证
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```
