# 工具箱（ToolBox）

[![CI](https://github.com/GaussAA/tools-box/actions/workflows/ci.yml/badge.svg)](https://github.com/GaussAA/tools-box/actions/workflows/ci.yml)

插件式桌面工具箱。主程序只是一个「外壳」：导航、搜索、收藏、配置存取；每个具体工具
都是独立的 DLL 插件，放进 `<exe 目录>/tools/` 就生效。**新增工具不需要修改外壳代码** ——
这是本项目的核心设计约束，任何改动都不能破坏它。

技术栈固定为 **Qt 6 Widgets + C++17 + CMake + MSVC 2022 x64**，采用 **MVP + 分层**
架构。现有工具看 `plugins/` 目录，或直接运行程序看左侧导航。

界面**跟随系统语言**：中文系统显示中文，其余显示英文（译文已随程序打包，不需要额外
文件）。想固定语言，用菜单 **帮助 → 界面语言**（跟随系统 / 中文 / English）点选即可，
点完提示重启生效；也可直接改配置 `ui/language`（`zh_CN` / `en`）—— 详见
[workflow.md §3.3](docs/workflow.md#33-界面语言与翻译)。

## 快速开始

需要 Windows x64、Visual Studio 2022 或更新（MSVC x64 工具集，含自带的 clang-format /
clang-tidy）、Qt 6.12.0 的 `msvc2022_64`、CMake ≥ 3.25。Qt 路径在本机不是
`C:/Qt6.12/6.12.0/msvc2022_64` 的话，改 `CMakePresets.json` 里那**一处**即可。

```powershell
cmake --preset default                                # 配置
cmake --build --preset debug                          # 构建
.\build\bin\Debug\ToolBox.exe                         # 运行
ctest --test-dir build -C Debug --output-on-failure   # 单元测试
```

钩子（**每个 clone 跑一次**）：让 git 用仓库里的 `.githooks/`，此后每次 `git commit`
会自动跑七个秒级检查，违规直接拦下 —— 免得等 CI 几分钟后才被告知（需要构建产物的
`verify_coretest` / `verify_naming` 不在钩子里，由 CI 在构建之后跑）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\install_hooks.ps1
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

**代码与文档不一致时，以文档为准。** 文档分两类，这里只列索引（不放副本）。
**权威规范**是项目长期有效的「宪法」；**分析与报告**是某次评估／迁移的记录，供追溯，规范一律以权威规范为准。

### 权威规范

| 文档 | 负责什么 |
| --- | --- |
| [architecture.md](docs/architecture.md) | 分层、职责边界、插件契约、偏差台账。**架构的唯一权威来源** |
| [coding-standards.md](docs/coding-standards.md) | 命名、内存、信号槽、字符串、CMake 等编码规则，以及每条规则靠什么保证 |
| [workflow.md](docs/workflow.md) | 构建、测试、加插件、发布、CI、文档变更流程 |

### 分析与报告

| 文档 | 内容 |
| --- | --- |
| [error_ledger.md](docs/error_ledger.md) | **持续维护**的错误台账：症状隔得很远的顽固问题，`[现象]→[根因]→[正解]→[守住]` 四段式，每条必须指明防止复发的手段 |
| [architecture-analysis-report.md](docs/architecture-analysis-report.md) | 架构与代码结构分析（实现层速览：类职责、信号槽、依赖图） |
| [best-practices-assessment.md](docs/best-practices-assessment.md) | Qt 最佳实践符合性评估（P0–P3 清单与落地记录） |
| [version-migration-audit.md](docs/version-migration-audit.md) | Qt 6.12 迁移深度审计（8 处问题、CI 结果与后续勘误） |

## 几条硬约束

这几条是项目刻意用工具或编译器锁住的，不依赖评审自觉（详见
[coding-standards.md §12](docs/coding-standards.md#12-检查手段映射表)）：

- **警告即错误**：MSVC `/W4 /permissive- /WX`，Debug 与 Release 都必须 0 告警。
- **纯逻辑必须能被单元测试覆盖**：凡是能脱离 `QWidget` 表达的逻辑都放进 `*/core/`，
  编成只链接 `Qt6::Core` 的静态库 —— 误用界面类会**直接编译失败**，不需要靠 grep 兜底。
- **格式、空白与命名由脚本强制**：全仓已用 clang-format 归一化并锁住，`scripts/verify/`
  下的八个脚本负责把关（格式与命名各钉一个工具版本，版本不符直接失败，不给假绿）。
  嫌逐个敲麻烦就跑 `scripts/verify/run_all.ps1`，它按顺序跑完这一套。
- **单文件规模也有脚本盯着**：源码超过 600 行必须拆分，或登记豁免并写明理由与上限
  （`scripts/verify/verify_filesize.ps1`）—— 只写在评审结论里的话，文件下次又会悄悄长回去。
- **CI 在每次 push / PR 跑**：六个秒级快检 + Debug/Release 双配置构建 + 双配置 `ctest` +
  命名检查 + 打包，产物作为 artifact 上传。

## 许可

本仓库的**代码**按 [MIT](LICENSE) 授权。

第三方下载内核（yt-dlp / FFmpeg）**不由本仓库分发**：它们由「视频下载」插件在首次
使用时从上游下载到 `<程序目录>/tools/bin/`，许可各自独立 —— yt-dlp 是 Unlicense，
FFmpeg 的 win64-gpl 构建是 **GPL v3**。若你把含 FFmpeg 的目录再分发给别人，
须自行满足 GPL 的要求。详见 [LICENSE](LICENSE) 末尾的「第三方组件」。
