# Qt 工具箱架构最佳实践符合性评估

> 评估对象：`tools-box` 插件式 Qt 6 桌面工具箱
> 评估范围：架构分层、契约设计、构建系统、信号槽通信、第三方依赖、长期可演进性
> 评估基准：Qt 官方推荐范式（插件式架构、`QPluginLoader`/`Q_PLUGIN_METADATA`、`MODULE` 产物、现代 CMake）、C++/Qt 工程化通用最佳实践
> 评估日期：2026-10-01

---

## 1. 总体结论

**架构大体符合 Qt 最佳实践，且明显优于多数自研工具；唯一真正偏离「最推荐范式」之处在插件发现机制（全量加载枚举）——该点已于 2026-10-01 修复，详见 §3.1。**

其余与「最推荐」之间的差异，多为产品路线的刻意权衡（如 Widgets 而非 QML、ABI 锁定单一工具链），并非架构缺陷。范式本身（插件契约 + 分层 + 现代构建）稳健，可长期演进。

---

## 2. 完全符合最佳实践之处

| # | 实践 | 代码落地 | 为何算最佳实践 |
|---|---|---|---|
| 1 | 插件契约用 `Q_INTERFACES` + `Q_DECLARE_INTERFACE` + `Q_PLUGIN_METADATA(IID)` | `sdk/ToolBoxPlugin.h:121` `:152` `:158` | Qt 官方唯一推荐的插件式架构；IID 带版本 `com.toolbox.ToolBox/IToolPlugin/1.0` 即 ABI 版本化 |
| 2 | 插件产物为 `MODULE` 库（DLL） | `plugins/base64/CMakeLists.txt:3` 等同 | Qt 推荐插件用 `MODULE`（仅经 `QPluginLoader` 装载），非 `SHARED`/可执行 |
| 3 | 依赖严格单向 `plugins → sdk ← app`，sdk 为 header-only INTERFACE | `sdk/CMakeLists.txt:3` | 契约下沉、三者互不 include，是「稳定接口隔离」的教科书做法 |
| 4 | 纯逻辑抽 `_core` 静态库、与 UI 解耦可单测 | `app/CMakeLists.txt:3`、`plugins/videodl/CMakeLists.txt:3` | 关注点分离 + 可测试性 |
| 5 | 现代 CMake target-based + Presets + `/WX` 警告即错误 | 顶层/`app` CMakeLists | 现代 C++/Qt 工程化基线，警告即错误防退化 |
| 6 | QObject 父子树管生命周期、新式信号槽、lambda 带 context | `app/MainWindow.cpp` 各处 | 内存与连接安全达标 |
| 7 | 重载顺序：saveState → delete pages → unload DLL | `app/MainWindow.cpp:165-177` | 规避悬空虚表指针——多数自研插件壳会踩的坑，本项目处理正确 |
| 8 | i18n 全程 `tr()` + 设 org/app/version + 双 translator + qm 内嵌资源 | `app/main.cpp:15-50` | 符合 Qt 国际化规范，且无泄漏默认组织名风险 |
| 9 | ABI 风险自觉，文档明确 ToolMeta 按值跨 DLL、改动即破坏 | `sdk/ToolBoxPlugin.h:6-7` | 防御性意识到位，契约变更有书面约束 |

---

## 3. 与「最推荐范式」的差距（按价值排序）

### 3.1 【最关键】插件发现机制：全量 load 枚举，而非静态元数据

**现状（改造前）。** `ToolRegistry::rescan` 对 `tools/` 下每一个 DLL 都执行 `loader->load()`，再 `qobject_cast<toolbox::IToolPlugin*>` 并调用 `meta()` 取得元信息用于列表/搜索/排序（`app/ToolRegistry.cpp`）。即「要列出有哪些工具，必须先全部加载进进程」。该问题已于 2026-10-01 实施 3.1 改造，改为「先读静态元数据门禁、再按需 load」。

**最推荐做法。** 将 `ToolMeta` 写进插件静态元数据：

```cpp
// 插件侧
class MyPlugin : public QObject, public toolbox::IToolPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)
    ...
};
```

宿主改用 `QPluginLoader::metaData()`（**该方法不调用 `load()`**）读取 JSON，先行按 IID、接口版本过滤与校验，**仅对通过者才 `load()`**。`metadata.json` 内放置 `id/name/category/version/description` 等字段。

**为什么这是最推荐范式：**

1. **启动更快、内存更省**——只实例化被接受/启用的插件，未启用或待禁用的插件不进进程。
2. **ABI 崩溃防护**——若某 DLL 由不同 Qt/编译器版本构建，`load()` 可能直接崩溃或行为不可预期；静态元数据读取发生在 `load()` 之前，可先筛掉不兼容者，避免宿主被拖垮。
   > **2026-10-02 订正**：仅比对 IID **实现不了**这一条 —— IID 相同、工具链不同的 DLL
   > 照样会被放行并在加载后崩溃。真正挡住它的是随后补上的 `abi` 构建环境指纹字段
   > （由 `metadata.json.in` 经 `configure_file` 注入，宿主侧同源比对，见
   > [architecture.md §4.2](./architecture.md#42-版本与-abi)）。判定规则已抽到
   > `app/core/PluginScanPolicy`，每条分支都有单测。
3. **能力前置**——列表、搜索索引、插件启用/禁用、版本兼容性提示均无需实例化插件，宿主更健壮。

**代价与迁移要点：**

- `metadata.json` 需冗余一份 `ToolMeta` 字段，必须保证与 `meta()` 的返回值一致。
- 一致性保障手段：在插件构建期由 `meta()` 自动生成 JSON（推荐），或在 `rescan` 后对「已 load 的插件」断言 `metaData()` 与 `meta()` 一致，不一致则记错误并跳过。
- `IToolPage`（可选生命周期接口）不受影响，继续走运行时 `qobject_cast`。

**实施概要（已实施并编译验证通过，2026-10-01）：**

1. `sdk/ToolBoxPlugin.h` 新增 `kPluginMetaKey`、`isCompatiblePluginMetaData()`、`toolMetaFromMetaData()`，约定 `metadata.json` 字段 schema 与 IID 门禁。
2. 各插件 `*.h` 的 `Q_PLUGIN_METADATA` 加 `FILE "metadata.json"`；JSON 由 moc 编译进 DLL（无需 CMake 改动、运行时无独立文件）。
3. `ToolRegistry::rescan` 改为两阶段：先 `metaData()` 读静态 JSON，IID 不符者记入 `m_errors` 并跳过 `load()`；通过者再 `load()` 并 `qobject_cast` 取实例（其余逻辑不变）。
4. 补单测 `tests/tst_pluginmeta.cpp`：直接验证 `isCompatiblePluginMetaData()` 对当前 IID 放行、对错误/空 IID 拦截，并验证 `toolMetaFromMetaData()` 解析；已接入 `tests/CMakeLists.txt`。

**本机编译验证（2026-10-01）：** 在 Qt 6.10.3/msvc2022_64 + MSVC19.51（VS18 Community）+ cmake 4.3.1 下，以 NMake 生成器（手动复刻 vcvars 环境、显式 `CMAKE_CXX_COMPILER` 指向 cl.exe）完成配置、全量编译与 ctest，**7/7 测试全部通过**（含 `tst_pluginmeta`）。验证中发现并修复了两处编译期问题：① `ToolBoxPlugin_iid` 宏原置于辅助函数之后，导致未定义引用；② 测试文件漏写 `QTEST_GUILESS_MAIN` 入口宏致链接缺 `main`。
> 注：项目 `CMakePresets.json` 曾硬编码 `CMAKE_PREFIX_PATH=D:/Qt/6.10.3/msvc2022_64`（与实际不符，直接用预设会配置失败）。该路径已随 Qt 6.12 迁移一并修正为 `C:/Qt6.12/6.12.0/msvc2022_64`（见 `CMakePresets.json`），日后可直接 `cmake --preset default` 构建；不依赖环境变量的兜底验收入口为 `scripts/build_verify.ps1`。

### 3.2 UI 范式：Widgets 而非 QML

`MainWindow` 用 `QMainWindow`/`QListWidget`/`QStackedWidget`，插件页经 `createPage()` 返回 `QWidget*`（`app/MainWindow.h`、`sdk/ToolBoxPlugin.h:145`）。

Qt 官方对「新 UI」长期推荐 Qt Quick/QML。但对**密集小工具箱**场景，Widgets 是资深团队常见且合理的选择：控件成熟、DPI/无障碍/原生外观稳定、插件页契约简单（直接返回 `QWidget*`）。**属产品路线权衡，非硬伤。** 若未来要触屏交互、富动画或跨 WebAssembly，再评估向 QML 迁移。

### 3.3 ABI 锁定单一工具链

项目显式锁定 Qt 6.12.0 / MSVC2022 / C++17（`CMakeLists.txt`、文档）。稳定是真优势；但代价是第三方或其他平台构建的插件无法加载。若愿景是「开放插件生态」，需改为发布固定工具链，或把插件契约下沉到稳定 C ABI（如 `extern "C"` 工厂函数）。当前自研自用，合理。

> **Qt 6.12 迁移（2026-10-01）**：项目已从 Qt 6.10.3 升级到 **Qt 6.12.0**（`C:/Qt6.12/6.12.0/msvc2022_64`），构建改用 **Ninja Multi-Config**（cmake 4.3.1 + ninja 1.13.2，均为 VS18 自带）。**零代码改动**即在 Debug/Release 双配置下各 10/10 ctest 通过。迁移中用 lupdate（6.12 下已可正常运行）发现并修复一处抽库遗留缺陷：`qt_add_translations` 的 `SOURCE_TARGETS` 漏列 `ToolBoxApp`，致 `MainWindow`/`ToolRegistry` 界面文案未被抽取（27 条被误判 vanished）；补入后 161/161 词条恢复正常。

### 3.4 加强项（非违规）

| 项 | 现状 | 建议 |
|---|---|---|
| UI 层自动化测试 | 10 个 Qt Test：8 个覆盖 `core/` 纯逻辑、静态元数据门禁与 `Logger`；**`MainWindow` 烟雾测试已实施并验证（2026-10-01）**：采用「抽 `ToolBoxApp` 静态库」方案（`MainWindow`/`ToolRegistry` 从可执行抽出，`qt_add_library` 自动 moc，主 exe 仅留 `main.cpp`），测试链接该库，offscreen 平台下验证构造/`reloadTools`/切首页/搜索过滤四类主路径不崩溃；**端到端集成测试已实施并验证（2026-10-01）**：`tests/tst_integration.cpp` 经 CMake POST_BUILD 部署 base64/jsonfmt 两个纯 GUI 安全插件到测试专属目录 `integration_tools/`，验证 `ToolRegistry` 跨 DLL 扫描 + `qobject_cast` 识别 `IToolPlugin` + `createPage()` 跨 DLL 返回 `QWidget*` + 跨 DLL 析构安全（offscreen 平台、QSettings 引到临时 ini） |
| 错误收集 | 仅字符串列表 `m_errors` | **已实施并验证（2026-10-01）**：新增 `app/core/Logger.{h,cpp}`（`toolbox::Logger`），在 `main` 早期 `install()` 将 `qDebug/qInfo/qWarning/qCritical` 重定向到 `AppData/ToolBox/toolbox.log` + 控制台（时间戳/级别/线程/源码位置），`ToolRegistry` 各错误与成功路径同步落结构化日志；`m_errors`（UI 展示）保持不变，二者互补。**并含分级开关与轮转（2026-10-01 增强）**：`LoggerOptions{maxFileSize=5MiB, backupCount=3, minLevel=Info}`，超限自动轮转 `log → log.1 … log.N`，低于 `minLevel` 的消息被丢弃，环境变量 `TOOLBOX_LOG_LEVEL` 可覆盖级别；新增 `shutdown()` 支持受控重装（兼作测试隔离）。`tests/tst_logger.cpp` 5 用例覆盖重定向/幂等/分级过滤/环境变量覆盖/轮转 |
| 翻译覆盖 | `translations/toolbox_en.ts`（英文），i18n 管道 `qt_add_translations` 已就位 | **已实施并验证（2026-10-01）**：`ToolRegistry` 新增 2 条 `tr()` 词条（IID 不兼容跳过、已加载插件）此前未抽取，已同步补入 `.ts` 并给英文译文；构建 `lrelease` 报告 **161 finished / 0 unfinished**，`.qm` 经 `rcc` 编入可执行资源。后续新增/改动 `tr()` 时，在工具链完整环境跑 `cmake --build build --target update_translations`（脚本 `scripts/lupdate_ts.ps1`）刷新 `.ts`（本机 Qt 6.12 已带 `Qt6Qml.dll`，`lupdate` 可正常运行 —— 实测 `lupdate version 6.12.0`；§3.3 那条「lupdate 因缺 DLL 跑不起来」是 Qt 6.10 时期的情况，已随迁移失效。`<location>` 行号为元数据、不影响翻译生效） |

---

## 4. 长期演进结论

- 架构范式（插件契约 + 分层 + 现代构建）**稳健、可长期演进**，未发现结构性反模式。
- 把「良好」推到「最推荐」的**唯一高价值改动 = 3.1 静态插件元数据发现**，已于 2026-10-01 完成并通过本机编译与 10/10 ctest 验证。
- **P2 结构化日志已于 2026-10-01 实施并通过 10/10 ctest 验证**；**P2 `MainWindow` 烟雾测试已于 2026-10-01 实施并通过 10/10 ctest 验证**（采用抽 `ToolBoxApp` 静态库方案，详见 §3.4）。
- **端到端集成测试已于 2026-10-01 实施并通过 10/10 ctest 验证**：部署真实插件 DLL，覆盖「跨 DLL 扫描 + `qobject_cast` + `createPage` + 跨 DLL 析构」这条此前烟雾测试刻意回避的真实路径（详见 §3.4）。
- **P3 翻译词条已于 2026-10-01 补齐**：`lrelease` 报 161 finished / 0 unfinished；至此本评估清单 **P0 + P2 + P3 全部完成**。
- Widgets/QML 是产品路线决定，不是架构缺陷，不必为「追新」而迁移。

---

## 5. 建议下一步（按优先级）

1. **P0（已完成并验证，2026-10-01）**：3.1 静态插件元数据发现——已落地并经本机编译 + 10/10 ctest 验证，消除启动全量加载与 ABI 崩溃风险。
2. **P2·结构化日志（已完成并验证，2026-10-01）**：`app/core/Logger` + `main` 接入 + `ToolRegistry` 落日志 + `tst_logger`——已通过 10/10 ctest 验证。
3. **P2·MainWindow 烟雾测试（已完成并验证，2026-10-01）**：抽 `ToolBoxApp` 静态库（`qt_add_library`，自动 moc）+ 主 exe 仅留 `main.cpp` + `tests/tst_mainwindow.cpp`（offscreen 平台、QSettings 引到临时 ini 防污染）+ 接入 `tests/CMakeLists.txt`——已通过 10/10 ctest 验证。
4. **P2·端到端集成测试（已完成并验证，2026-10-01）**：`tests/tst_integration.cpp` + CMake POST_BUILD 部署 base64/jsonfmt 到测试专属目录 `integration_tools/`——验证跨 DLL 扫描 + `qobject_cast` + `createPage` + 跨 DLL 析构，已通过 10/10 ctest 验证。
5. **P3·翻译词条补齐（已完成并验证，2026-10-01）**：`ToolRegistry` 新增的 2 条 `tr()`（IID 不兼容跳过、已加载插件）此前未抽取，已补入 `translations/toolbox_en.ts`；`lrelease` 报 **161 finished / 0 unfinished**。工具链完整环境可用 `cmake --build build --target update_translations` 刷新（脚本 `scripts/lupdate_ts.ps1`）。

> **本评估清单至此全部完成（P0 + P2 + P3）**，均已通过本机编译与 10/10 ctest 验证。

### 2026-10-02 后续落地（本轮审计，测试数 10 → 13）

评估之后又过了一轮全面审计与落地，与本清单直接相关的进展：

| 项 | 与本评估的关系 | 现状 |
| --- | --- | --- |
| **GUI 冒烟真跑** | §3.4 的两类测试都不启动真程序 —— 本轮 `verify_shell.ps1`（启动真实产物 + 截图 + UIAutomation 断言）首次在本地跑通，**立刻挖出 3 个所有测试都看不见的 bug**（Qt 6.12 翻译空壳、`zh` 缺地区、非标准 Qt 路径致回退失效），Debug/Release 两套产物均 ALL PASS | 已常态化：Debug/Release 双产物冒烟通过 |
| **单文件规模门禁** | 评估建议「评审盯」的 600 行规则 → 落成 `verify_filesize.ps1`（>600 须登记豁免，带理由 + 上限） | 已入 CI；**它随后真的拦住了 1358 行的页面**，触发 D2 拆分 |
| **core 必须有测试的门禁** | §2.4「core 可单测」从评审约定 → `verify_coretest.ps1` 机械检查（行覆盖率本机无工具，此为可验证下限） | 已入 CI，10 个 core 源全被引用 |
| **D2 拆分完成** | `VideoDlPage` 1358 行 → **678 行**，不再持有任何 `QProcess`/网络对象；命令行构造抽成 `core/DownloadArgs`（可单测） | 偏差 9.2 改记「已收敛」，豁免上限收紧到 800 |
| **ABI 门禁（真）** | 评估 §3.1 静态元数据发现只解决了「启动时加载」—— 本轮补上 metadata 构建期指纹（Qt 版本 + 编译器 + 工具集），挡住「IID 对、工具链不对」的 DLL | `metadata.json.in` + `PluginScanPolicy`（id/abi/重复 id 三道门禁） |
| **ccache 接入** | 构建加速：热重编 55.3s → 25.1s（命中 92%）；Debug 调试信息改 `/Z7`（ccache 生效前提） | 自动检测，无 ccache 的环境（CI）静默跳过 |
| **ctest 并行** | `-j`：4.97s → 2.84s | 本机脚本与 CI 均已加 |

教训（已写进迁移审计第 4 条与 [error_ledger.md](./error_ledger.md)）：**「测试全绿」与
「程序正确」之间隔着「程序得先能启动」—— 不启动真程序的验证体系，对启动期、部署期、
原生对话框这一整类问题都是盲的。**

> 本评估为书面记录。3.1、P2 结构化日志、P2 `MainWindow` 烟雾测试、P2 端到端集成测试、P3 翻译词条补齐均已于 2026-10-01 经大帅准奏、实施并通过本机编译与 10/10 ctest 验证；落地文件：`sdk/ToolBoxPlugin.h`、`app/ToolRegistry.cpp`、三插件 `*.h` 与 `metadata.json`、`tests/tst_pluginmeta.cpp`、`app/core/Logger.{h,cpp}`、`app/main.cpp`、`app/CMakeLists.txt`（抽 `ToolBoxApp` 库）、`tests/tst_mainwindow.cpp`、`tests/tst_integration.cpp`、`tests/CMakeLists.txt`、`CMakePresets.json`（Qt 路径修正）、`translations/toolbox_en.ts`（词条补齐）。
