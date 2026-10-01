# 版本迁移深度审计报告（Qt 6.10.3 → 6.12.0）

> 日期：2026-10-01 ｜ 范围：全仓库 ｜ 方法：六维扫描（版本残留 / 构建现代性 / Qt API / 死代码 / 文档一致性 / 端到端验证）

本报告记录从 Qt 6.10.3 迁移到 Qt 6.12.0 的**完整性核实**：确保无旧版本残留、无死代码、且不存在「为兼容而将就」的次优写法。审计不止于「能编译」，而是逐项比对 Qt 6.12 的官方要求与项目实际。

## 1. 审计方法

| 维度 | 手段 |
| --- | --- |
| 版本 / 路径残留 | 全仓 grep（**含 `.github` 等隐藏目录**）：旧版本号、旧 Qt 路径、旧工具集 / 生成器 |
| 构建系统现代性 | 逐文件审查 `CMakeLists.txt` / `CMakePresets.json`，比对 Qt 6.12 官方要求 |
| Qt API 弃用 | 扫描 `QLibraryInfo` / `QRegExp` / HighDpi 等 Qt 6 弃用点 |
| 死代码 / 残留 | 目录树、备份 / 临时文件、构建缓存残留 |
| 文档一致性 | 版本号 / 路径跨文档核对；跑 `verify_docs` 链接与孤儿检查 |
| 端到端验证 | Qt 6.12 + Ninja Multi-Config 全量构建 + Debug/Release 双配置 `ctest` |

## 2. 发现与修复

| # | 级别 | 问题 | 位置 | 处置 |
| --- | --- | --- | --- | --- |
| 1 | P0 | CI 仍安装 Qt **6.10.3** | `.github/workflows/ci.yml` | 改为 `6.12.0` |
| 2 | P0 | `cmake_minimum_required 3.21` 低于 Qt 6.12 要求的 **3.25** | `CMakeLists.txt`、`CMakePresets.json` | 提升至 `3.25` |
| 3 | P0 | `build_probe/` 构建缓存残留 | 项目根 | 删除 |
| 4 | P0 | 中文文件名 `架构分析报告.md` 使 git `core.quotePath` 输出转义串，`verify_whitespace` / `verify_docs` 当作路径报「非法字符」而中断（**潜伏的 CI 门禁破坏**） | 项目根 | 重命名为 `architecture-analysis-report.md` |
| 5 | P1 | `docs/best-practices-assessment.md` 为**孤儿文档**（未被任何文档链接，`verify_docs` 报 FAIL） | `docs/` | 在 `architecture.md` 与 `README.md` 的文档索引补链接 |
| 6 | P1 | `tst_logger.cpp` / `tst_pluginmeta.cpp` 的 `#include "*.moc"` **之后仍有代码**（`QTEST_GUILESS_MAIN`），违反 coding-standards 且不符 Qt 标准写法 | `tests/` | 宏前移，moc include 收尾 |
| 7 | P1 | 文档称 `MSVC v143 (VS2022)` / `CMake ≥ 3.21`，与现实（VS18 / v144）及 Qt 6.12 要求不符 | `README.md`、`docs/workflow.md`、`architecture-analysis-report.md` | 更新为「VS2022 及以上 / CMake ≥ 3.25」 |
| 8 | P2 | `qt_standard_project_setup()` 未声明最低 Qt 版本 | `CMakeLists.txt` | 改为 `qt_standard_project_setup(REQUIRES 6.12)` |

## 3. 验证结果

- **构建**：Qt 6.12.0 + Ninja Multi-Config（cmake 4.3.1 / ninja 1.13.2 / MSVC 19.51）——配置成功、Debug + Release 双配置构建通过、**双 `ctest` 各 10/10**、`lrelease` **161/161**。
- **规范门禁**：`verify_whitespace` / `verify_docs` / `verify_conventions` / `verify_naming` 全部 **ALL PASS**。
- **Qt API**：无弃用用法（`QLibraryInfo::path()` 已是 Qt 6 现代 API）。
- **CI（GitHub Actions，干净机器）**：**全绿**（run `36868739998`）——whitespace / format / conventions / docs 四道快检、Qt 6.12.0 安装、MSVC 环境、Debug + Release 双配置构建、`ctest`、`windeployqt`、打包与上传全部通过。

## 4. 结论

- 迁移**彻底**：无旧版本号、无旧路径、无旧生成器残留。
- **未发现「为兼容而将就」的次优写法**；反而借本次核实**修正了两处历史遗留缺陷**（孤儿文档、moc include 顺序）与一处**潜伏 CI 隐患**（中文文件名）。
- 项目现锚定 **Qt 6.12.0 / CMake ≥ 3.25 / Ninja Multi-Config**，全部门禁绿。

## 5. 备注

- `verify_shell.ps1` / `verify_recent.ps1` 在**同一 PowerShell 进程内连续执行多个脚本**时会因重复 `Add-Type` 报错（类型已存在）；CI 中每个脚本独立进程，不受影响——这是本机批量执行方式的副作用，非项目缺陷。
- `scripts/build_verify_nmake.ps1` 为 NMake 单配置兜底通道（ninja 不可用时使用），**有意保留**；主验收通道为 `scripts/build_verify.ps1`（Ninja Multi-Config）。
- **CI 安装 Qt 的方式**：`aqtinstall` 最新发布版（3.3.0）**尚不支持 Qt 6.11+**——该支持只在其 CHANGELOG 的「Unreleased」段（"Support Qt 6.11+ for Windows X64" #1000），因此用 `jurplel/install-qt-action@v4` 装 6.12.0 会报 `Failed to locate XML data for Qt version '6.12.0'`。故 `.github/workflows/ci.yml` 改为**直接安装 aqtinstall 主干**并手动调用 `aqt install-qt`；待含该支持的正式版发布后，可改回 `install-qt-action`。
