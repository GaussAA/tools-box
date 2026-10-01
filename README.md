# 工具箱（ToolBox）

[![CI](https://github.com/GaussAA/tools-box/actions/workflows/ci.yml/badge.svg)](https://github.com/GaussAA/tools-box/actions/workflows/ci.yml)

插件式桌面工具箱。主程序只是一个「外壳」：导航、搜索、收藏、配置存取；每个具体工具
都是独立的 DLL 插件，放进 `<exe 目录>/tools/` 就生效。**新增工具不需要修改外壳代码** ——
这是本项目的核心设计约束，任何改动都不能破坏它。

技术栈固定为 **Qt 6 Widgets + C++17 + CMake + MSVC 2022 x64**，采用 **MVP + 分层**
架构。现有工具看 `plugins/` 目录，或直接运行程序看左侧导航。

## 快速开始

需要 Windows x64、Visual Studio 2022（含 MSVC v143，以及它自带的 clang-format /
clang-tidy）、Qt 6.10.3 的 `msvc2022_64`、CMake ≥ 3.21。Qt 路径在本机不是
`D:/Qt/6.10.3/msvc2022_64` 的话，改 `CMakePresets.json` 里那**一处**即可。

```powershell
cmake --preset default                                # 配置
cmake --build --preset debug                          # 构建
.\build\bin\Debug\ToolBox.exe                         # 运行
ctest --test-dir build -C Debug --output-on-failure   # 单元测试
```

打一个「解压即用」的 zip：

```powershell
cmake --build --preset release
cmake --build build --config Release --target deploy
cpack --config build\CPackConfig.cmake -C Release -B build\package
```

## 加一个新工具

复制 `plugins/base64/` 改名即可，八步流程见
[workflow.md §4](docs/workflow.md#4-新增一个工具插件)。**不允许**为了让新工具跑起来去改
`app/` —— 需要改外壳说明契约不够，应先走契约变更流程。

## 文档

**代码与文档不一致时，以文档为准。** 三份文档各有分工，这里不放它们的副本：

| 文档 | 负责什么 |
| --- | --- |
| [architecture.md](docs/architecture.md) | 分层、职责边界、插件契约、偏差台账。**架构的唯一权威来源** |
| [coding-standards.md](docs/coding-standards.md) | 命名、内存、信号槽、字符串、CMake 等编码规则，以及每条规则靠什么保证 |
| [workflow.md](docs/workflow.md) | 构建、测试、加插件、发布、CI、文档变更流程 |

## 几条硬约束

这几条是项目刻意用工具或编译器锁住的，不依赖评审自觉（详见
[coding-standards.md §12](docs/coding-standards.md#12-检查手段映射表)）：

- **警告即错误**：MSVC `/W4 /permissive- /WX`，Debug 与 Release 都必须 0 告警。
- **纯逻辑必须能被单元测试覆盖**：凡是能脱离 `QWidget` 表达的逻辑都放进 `*/core/`，
  编成只链接 `Qt6::Core` 的静态库 —— 误用界面类会**直接编译失败**，不需要靠 grep 兜底。
- **格式与空白由脚本强制**：全仓已用 clang-format 归一化并锁住，`scripts/verify/` 下
  的脚本负责把关。
- **CI 在每次 push / PR 跑**：四个秒级快检 + Debug/Release 双配置构建 + `ctest` +
  打包，产物作为 artifact 上传。
