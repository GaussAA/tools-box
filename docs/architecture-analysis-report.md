# 工具箱（ToolBox）Qt 项目 · 架构与代码结构分析报告

> 分析对象：`tools-box/`（插件式桌面工具箱）
> 分析日期：2026-10-01
> 依据：项目源码 + 官方文档 `docs/architecture.md` / `docs/coding-standards.md`
> 说明：架构以 `docs/architecture.md` 为唯一权威来源；以下梳理与之对齐并补入实现层细节。
>
> ⚠️ **本文是 2026-10-01 的一次性分析快照**，用于快速了解实现层细节（类职责、信号槽、依赖图）。
> 规范结论一律以 `docs/architecture.md` 为准；计数类数据（如测试数量）会随开发推进而滞后，
> 最新清单见 `docs/workflow.md §5`。

---

## 0. 项目定位与技术栈速览

| 项          | 内容                                                                                                                                                    |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 定位        | 插件式桌面工具箱。主程序只是「外壳」（导航/搜索/收藏/配置），每个工具是独立 DLL 插件，放进`<exe>/tools/` 即生效。**新增工具不修改外壳**是核心设计约束。 |
| 架构范式    | MVP + 分层（传统 Widgets 桌面项目；不采用 QML/MVVM）                                                                                                    |
| 语言/标准   | C++17、Qt 6 Widgets                                                                                                                                     |
| 编译器/构建 | MSVC x64（VS2022 及以上）+ Ninja Multi-Config；CMake ≥ 3.25                                                                                             |
| 测试        | Qt Test + CTest，纯逻辑单测覆盖                                                                                                                         |
| 交付        | CPack 出「解压即用」zip（含 Qt 运行时 + 翻译 + 外部内核）                                                                                               |

---

## 1. 目录与分层组织

```
tools-box/
├── CMakeLists.txt          顶层：统一 Qt 依赖、编译选项、输出布局、显式列出子目录、打包
├── CMakePresets.json       唯一构建入口（MSVC2022 x64 / Qt 6.12.0 / Ninja Multi-Config）
├── docs/                   权威规范（architecture / coding-standards / workflow）+ 分析报告
├── scripts/verify/         开发期验证脚本（powershell：格式/空白/命名/跨层依赖/外壳验收）
├── sdk/                    契约层：纯头文件 INTERFACE 库（无二进制）
│   └── ToolBoxPlugin.h     唯一跨模块契约：ToolMeta / ToolSettings / IToolPage / IToolPlugin
├── app/                    外壳层：主程序（ToolBox.exe → ToolBoxApp → ToolBoxCore / ToolBox::Sdk）
│   ├── main.cpp            应用元信息 + 入口 + 语言/翻译器安装 + 结构化日志安装
│   ├── MainWindow.*        窗口装配、导航渲染、配置持久化（View）
│   ├── ToolRegistry.*      插件扫描与装载（Service）；与 MainWindow 一起编成静态库 ToolBoxApp
│   └── core/               外壳纯逻辑（无 QWidget 依赖）→ 静态库 ToolBoxCore
│       ├── ToolCatalog.*   导航过滤、收藏/最近使用维护
│       └── LanguageChoice.* 界面语言解析
├── plugins/<工具名>/        工具层：一个工具 = 一个 MODULE 库（DLL）
│   ├── <Tool>Plugin.*      插件入口：meta() + createPage()
│   ├── （页面类常内联于 .cpp）
│   └── core/               该工具纯逻辑（无 QWidget 依赖）→ <工具>_core 静态库
├── translations/           toolbox_en.ts（英文译文源，lupdate 抽取）
└── tests/                  Qt Test 用例，一个测试一个目标，CTest 驱动
```

**分层判定核心**：`core/` 是约定名 —— 凡能用「不依赖 QWidget」表达的逻辑必须落在 `core/`，以便单测。`tools-box` 的 `core/` 静态库只链接 `Qt6::Core`，误用任何界面类会**直接编译失败**（不靠 grep 兜底）。

### 三层角色与依赖方向（不可违反）

| 层         | 职责                               | 禁止                                            |
| ---------- | ---------------------------------- | ----------------------------------------------- |
| `sdk/`     | 定义主程序与插件的唯一契约         | 依赖`app/` 或 `plugins/`；含任何实现代码        |
| `app/`     | 装载插件、装配外壳、托管页面与配置 | include 任何具体插件头；知晓具体工具行为        |
| `plugins/` | 实现单个工具                       | 依赖`app/` 头；插件互依赖；直接访问其它插件数据 |

**依赖方向固定为 `plugins → sdk ← app`**：插件只通过 `sdk/ToolBoxPlugin.h` 认识外壳；外壳只通过 `IToolPlugin` / `IToolPage` 认识插件。两者**不互相 include**。

---

## 2. 核心类及其职责

### 2.1 契约层（`sdk/ToolBoxPlugin.h`，INTERFACE 库）

| 类型                    | 性质             | 职责                                                                                                                                                                           |
| ----------------------- | ---------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `toolbox::ToolMeta`     | 结构体（值类型） | 工具元信息：`id / name / category / version / description / icon`。**按值跨 DLL 传递，只允许末尾追加字段，改类型/重排即破坏 ABI**。`id` 一旦发布永久不可改（兼作配置键前缀）。 |
| `toolbox::ToolSettings` | 类               | 工具专属配置存取。按`ToolMeta::id` 自动加 `plugin/<id>/` 前缀隔离，插件只写裸键名；底层即 `QSettings`（Windows 落注册表）。                                                    |
| `toolbox::IToolPage`    | 可选接口         | 页面生命周期钩子：`restoreState()` / `saveState()`。页面继承 `QWidget` + 此接口并声明 `Q_INTERFACES`。                                                                         |
| `toolbox::IToolPlugin`  | 主接口           | 插件入口：`meta()`（无副作用、同次装载稳定）、`createPage()`（返回新实例，所有权归外壳）。IID 版本化 `com.toolbox.ToolBox/IToolPlugin/1.0`。                                   |
| 宏                      | —                | `ToolBoxPlugin_iid`、`ToolBoxToolPage_iid`、`Q_DECLARE_INTERFACE`                                                                                                              |

### 2.2 外壳层（`app/`）

| 类                                | 父类             | 职责                                                                                                                                                                                                                                                                                            |
| --------------------------------- | ---------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `MainWindow`                      | `QMainWindow`    | **View + 装配器**。左侧导航（搜索框 + 列表）+ 右侧 `QStackedWidget` 页面容器。负责：从 `ToolRegistry` 取插件列表 → 为每个插件创建页并塞入 Stack → 导航选中切换页面 → 搜索过滤 → 维护「收藏 / 最近使用」两个分区 → 关闭前 `saveState`。**不认识任何具体工具**。                                  |
| `ToolRegistry`                    | `QObject`        | **Service（插件仓库）**。扫描 `<exe>/tools` 目录，用 `QPluginLoader` 装载实现 `IToolPlugin` 的 DLL；装载时**取一次 `meta()` 缓存**进 `Entry`（避免排序比较器 O(n log n) 次跨 DLL 虚调用）；按「分类 → 名称」排序；收集加载失败信息到 `errors()`。持有 `QPluginLoader` 生命周期，保证 DLL 驻留。 |
| `toolbox::ToolCatalog`（core）    | 命名空间自由函数 | 纯逻辑：`filterNavRows()` 关键字过滤 + 标题行随可见性收敛；`pruneMissingIds()` 剔除失效工具 id；`promoteRecent()` 最近使用提权 + 裁剪。与 `QListWidget` 解耦，可单测。                                                                                                                          |
| `toolbox::LanguageChoice`（core） | 命名空间自由函数 | 纯逻辑：`resolveUiLanguage()` 解析 `ui/language` 配置（空=跟随系统，认 `en*`/`zh*`），只判断不构造 `QSettings`/`QTranslator`，可脱离 `QApplication` 单测。                                                                                                                                      |
| `toolbox::Logger`（core）         | 单例（静态）     | 结构化日志：`install()` 把 `qDebug/qInfo/qWarning/qCritical` 重定向到文件 + 控制台，支持**分级过滤**（`TOOLBOX_LOG_LEVEL` 覆盖）与**按大小轮转**（`log → log.1 … log.N`）；`shutdown()` 供受控重装。纯逻辑，可单测。                                                                            |

### 2.3 工具层（`plugins/`）

| 插件      | 入口类             | 页面                                   | 说明                                                                                                            |
| --------- | ------------------ | -------------------------------------- | --------------------------------------------------------------------------------------------------------------- |
| `base64`  | `Base64Plugin`     | `Base64Page`（内联，实现 `IToolPage`） | 模板插件。`id=text.base64`。UTF-8↔Base64 互转，URL 安全字符集开关；状态经 `ToolSettings` 持久化。               |
| `jsonfmt` | `JsonFormatPlugin` | 内联匿名页                             | `id=dev.json-format`。故意不设 icon 以验证外壳首字符占位图标。JSON 格式化/压缩 + 语法错误定位。                 |
| `videodl` | `VideoDlPlugin`    | `VideoDlPage`（实现 `IToolPage`）      | `id=media.video-download`。驱动 yt-dlp 下载 B站/YouTube/抖音 等；内核下载 + 断点续传 + 抖音借浏览器渲染取直链。 |

### 2.4 视频下载插件的纯逻辑库 `videodl_core`

| 模块            | 职责                                                                                                                           |
| --------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `EngineLocator` | 内核定位规则：`<exe>/tools/bin` 目录计算 + 「手动指定 → 随程序目录 → PATH」优先级解析（可单测文件布局）。                      |
| `OutputParsing` | 解析 yt-dlp 外部输出：UTF-8/本地编码解码、剥 ANSI 色码、URL 提取、进度/阶段（`[download]`/合并/转码）识别、最终产物路径提取。  |
| `DouyinSupport` | 抖音专线纯规则：站点判定、画质→ratio 映射、渲染后 DOM 取`video_id`/标题。                                                      |
| `CookieFile`    | 浏览器导出 cookies.txt 规范化（修`domain` 开头 `.` 与 `includeSubDomains` 不一致的断言陷阱、补畸形行），避免 yt-dlp 整份拒收。 |

### 2.5 测试层（`tests/`）

10 个 Qt Test 目标。其中**纯逻辑测试**只链接被测的 `*_core` 静态库（不依赖 `QApplication`/真实网络/子进程）：
`tst_toolcatalog`、`tst_languagechoice`、`tst_pluginmeta`、`tst_logger`、`tst_outputparsing`、`tst_douyinsupport`、`tst_cookiefile`、`tst_enginelocator`；
另有 `tst_mainwindow`（GUI 烟雾，链接 `ToolBoxApp`）与 `tst_integration`（跨 DLL 端到端）。

---

## 3. 类间依赖与信号槽通信机制

### 3.1 通信分两路：**契约多态（跨层）** 与 **信号槽（层内）**

**跨模块（外壳 ↔ 插件）只用契约多态，不用信号槽：**

- 装载：`ToolRegistry` 用 `QPluginLoader` 加载 DLL → `loader->instance()` → `qobject_cast<toolbox::IToolPlugin*>`。
- 创建页：`entry.plugin->createPage(parent)` 返回 `QWidget*`，所有权移交外壳（外壳负责销毁）。
- 状态存取：`qobject_cast<toolbox::IToolPage*>(page)` 成功后调用 `restoreState()/saveState()`，传入 `ToolSettings(id)`。
- **外壳与插件之间没有任何直接信号槽连接** —— 解耦完全靠 SDK 接口与虚函数。这保证「新增工具不改外壳」。

**层内信号槽（Qt 新式语法，lambda 必带 context 对象）：**

#### 外壳 MainWindow（View 内部）

| 信号                                      | 槽                                    | 作用                                                           |
| ----------------------------------------- | ------------------------------------- | -------------------------------------------------------------- |
| `QListWidget::currentRowChanged`          | `onNavRowChanged`                     | 切`QStackedWidget` 页；记 `ui/lastToolId`；`noteRecent()`      |
| `QListWidget::customContextMenuRequested` | `onNavContextMenu`                    | 右键菜单切换收藏                                               |
| `QLineEdit::textChanged`                  | `onSearchTextChanged` → `applyFilter` | 导航关键字过滤 + 状态栏计数                                    |
| 菜单`QAction::triggered`                  | `reloadTools` / `showAbout`           | 重载插件（F5）/关于                                            |
| `QTimer::singleShot(0)`                   | 内联 lambda →`rebuildNav`             | 把重入风险的中重建推迟到事件循环下一轮（`scheduleNavRebuild`） |

#### 视频页 VideoDlPage（View + 子进程/网络 驱动）

| 信号                                           | 槽                        | 作用                                                           |
| ---------------------------------------------- | ------------------------- | -------------------------------------------------------------- |
| `QProcess(m_process)::readyReadStandardOutput` | `onProcessOutput`         | 读取 yt-dlp 输出，按行解析进度/阶段                            |
| `QProcess(m_process)::finished`                | `onProcessFinished`       | 收尾、状态/进度重置、清理 cookies 副本                         |
| `QProcess(m_process)::errorOccurred`           | lambda                    | `FailedToStart` → 提示内核路径无效                             |
| `QProcess(m_render)::readyReadStandardOutput`  | `onRenderOutput`          | 累积浏览器`--dump-dom` 整份 DOM                                |
| `QProcess(m_render)::finished`                 | `onRenderFinished`        | 解析`video_id`/标题 → 换直链 → 调 `launchDownload`             |
| `QProcess(m_render)::errorOccurred`            | lambda                    | 启动失败 → 置超时标志并复用`onRenderFinished`                  |
| `QNetworkReply::downloadProgress`              | `onFetchProgress`         | 内核下载进度（含断点续传偏移补偿）                             |
| `QNetworkReply::readyRead`                     | lambda                    | 写分片到`.part` 临时文件                                       |
| `QNetworkReply::finished`                      | lambda →`onFetchFinished` | 续传/重试（最多 5 次）/收尾                                    |
| `QPushButton::clicked`                         | 各业务方法                | 开始/取消下载、下载/指定内核、选 cookies                       |
| `QTimer::singleShot`                           | lambda                    | 重试退避、抖音渲染 60s 超时（用`generation` 防过期定时器误杀） |

> 重入防护范式：导航过滤/重建期间用 `m_applyingFilter` 守卫抑制 `currentRowChanged` 副作用（`onNavRowChanged` 内显式 return）；需要重建但处于回调中时推到下一轮事件循环。

### 3.2 关键交互流程

**① 启动装载**
`main.cpp`（设组织名/应用名/版本 → 解析语言 → 安装 `QTranslator`）→ `MainWindow` 构造（读 `ui/favorites`/`ui/recent` → `buildUi` → `buildMenus` → `reloadTools`）→ `ToolRegistry::rescan()`（扫 `tools/*.dll` → `QPluginLoader` → `qobject_cast<IToolPlugin>` → 缓存 `meta()` → 排序）→ 逐插件 `createPage` 入 `QStackedWidget` + `restoreState` → `rebuildNav()` 生成「首页/收藏/最近使用/分类」分区 → `applyFilter` 整理隐藏态。

**② 导航切换 / 搜索 / 收藏**：均为 MainWindow 内部对 `QStackedWidget` + `QListWidget` 的操作；过滤规则委托 `ToolCatalog::filterNavRows`（纯逻辑）。

**③ 视频下载**：`startDownload` → `extractUrl` 抠地址 → `beginDouyinDownload`（抖音走浏览器渲染取直链，否则直接 `launchDownload`）→ `m_process` 跑 yt-dlp → `onProcessOutput` 逐行解析进度/阶段 → `onProcessFinished` 收尾。内核缺失时 `fetchYtDlp/fetchFfmpeg` 经 `QNetworkAccessManager` 下载 + 断点续传 + `Expand-Archive` 解压。

**④ 重载/卸载（顺序不可颠倒）**：`saveAllToolStates()`（先存页面状态）→ 销毁全部页面（此时 DLL 仍在，虚表有效）→ `ToolRegistry::rescan()` 卸载旧 DLL、加载新 DLL。插件卸载后再碰页面虚表即悬空。

---

## 4. 构建系统配置（CMake）

- **入口**：`CMakePresets.json`（generator=Ninja Multi-Config；`CMAKE_PREFIX_PATH=C:/Qt6.12/6.12.0/msvc2022_64` 全仓唯一 Qt 路径）。
- **顶层 `CMakeLists.txt`**：
  - `project(ToolBox VERSION 0.2.0)`（版本号唯一来源）。
  - `find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Test LinguistTools)`。
  - MSVC 强制 `/utf-8`、`/W4 /permissive- /WX`（警告即错误）。
  - 统一输出布局 `TOOLBOX_BIN_DIR=build/bin/<Config>`，DLL 落 `tools/`。
  - 显式 `add_subdirectory`（sdk/app/各插件/tests），不用 glob。
  - `qt_add_translations`（英文译文编进 `:/i18n/toolbox_en.qm`）。
  - 顶层整目录 `install()` + CPack ZIP；两条 `install(CODE)` 拦截「非 Release」与「未 deploy」。
- **各子目录 target-based 写法**：
  - `sdk` → `ToolBoxSdk` INTERFACE（仅暴露头文件 + 链接 `Qt6::Widgets`）。
  - `app` → `ToolBoxCore` STATIC（只链 `Qt6::Core`）+ `ToolBoxApp` STATIC（`MainWindow.*` + `ToolRegistry.*`，链 `ToolBox::Sdk ToolBoxCore Qt6::Widgets`）+ `ToolBox` WIN32 可执行（只含 `main.cpp`，链 `ToolBoxApp`）；自定义 `deploy` 目标用 `windeployqt --no-opengl-sw --no-compiler-runtime` + app-local VC 运行时（跳过 17.9 MB 的 `vc_redist` 安装器，改拷约 1.6 MB 的 CRT DLL）。
  - `plugins/<x>` → `<x>_tool` MODULE（链 `ToolBox::Sdk`，DLL 落 `tools/`）；带纯逻辑的再编 `<x>_core` STATIC。videodl 额外链 `Qt6::Network`。图标经 `qt_add_resources` 编进 DLL。
  - `tests` → `toolbox_add_test()` 函数，链接 `Qt6::Test` + 被测 `*_core`，`RUNTIME_OUTPUT_DIRECTORY` 指向 `build/tests/<Config>`（避免混进交付目录），并前置 Qt bin 目录进 PATH。
- **约定**：`install()` 只出现在顶层；子目录不写 `install(TARGETS)`（保持交付描述单一、新增插件无需改顶层）。

---

## 5. 关键第三方依赖

### 5.1 编译期/链接期

| 依赖                                                    | 版本/形式                            | 用途                                        |
| ------------------------------------------------------- | ------------------------------------ | ------------------------------------------- |
| Qt 6（Core / Widgets / Network / Test / LinguistTools） | 6.12.0`msvc2022_64`                  | 全部 UI、信号槽、插件装载、网络、翻译、测试 |
| MSVC 工具集                                             | x64（VS2022 及以上；本机 VS18/v144） | 编译/链接；`/W4 /permissive- /WX` 锁零告警  |
| Ninja                                                   | Multi-Config                         | 构建后端                                    |
| CMake                                                   | ≥ 3.25                               | 构建系统（Qt 6.12 要求）                    |
| clang-format / clang-tidy                               | 钉版：clang-format 22.1.3 精确 / clang-tidy 22.1 发行线（CI 用 pip 装，本地可用 VS 自带） | 格式与命名保证（`verify_format` / `verify_naming`） |
| CPack                                                   | 随 CMake                             | ZIP 打包                                    |
| PowerShell                                              | 系统（含 BOM 脚本）                  | `Expand-Archive` 解压 ffmpeg、验证脚本      |

### 5.2 运行期外部内核（非本工程构建，随程序分发或系统提供）

| 依赖                        | 来源                                                                   | 用途                                                      |
| --------------------------- | ---------------------------------------------------------------------- | --------------------------------------------------------- |
| `yt-dlp.exe`                | 程序内「下载/更新」按钮从 GitHub releases 拉取；或系统 PATH / 手动指定 | 视频下载核心引擎                                          |
| `ffmpeg.exe`                | 同上（GitHub FFmpeg-Builds 的 zip，解压后挑`bin/ffmpeg.exe`）          | 音视频合并、仅音频转 mp3                                  |
| 无头浏览器（Edge / Chrome） | 系统已装即可，无需随包                                                 | 抖音渲染取`video_id`（yt-dlp 未实现抖音签名接口，必 403） |

> 内核定位顺序固定为：**手动指定 → `<exe>/tools/bin/` → PATH**。整个 `bin/<Config>/` 拷走即可运行（Qt 运行时 + 翻译 + 内核均在内）。

### 5.3 依赖方向约束（工具链层面）

主程序与所有插件**必须同一套 Qt、同一编译器、同一 C++ 标准**，否则共享 `QWidget` 对象二进制不兼容会直接崩溃（`CMakeLists` 注释明确警示）。

---

## 6. 测试与质量门禁

- **单测**：`tests/` 下 10 个目标——`ToolCatalog`（过滤/去重/最近使用）、`LanguageChoice`（语言解析）、`OutputParsing`/`DouyinSupport`/`CookieFile`/`EngineLocator`（解析与定位规则）、`PluginMeta`（静态元数据门禁）、`Logger`（日志重定向/分级/轮转），外加 `MainWindow`（GUI 烟雾）与跨 DLL 端到端集成。均由 `ctest` 驱动；`CI` 在 push/PR 跑四个快检 + Debug/Release 双配置构建 + 双配置 `ctest` + 命名检查 + 打包。
- **保证手段优先级**：编译器（`/WX`、弃用 API 直接失败）> 脚本（五个 `verify_*.ps1` 全部已进 CI）> 评审（tr 完备性、对象所有权需语义判断）。命名检查（clang-tidy）钉 **LLVM 22.1 发行线**：精确版本在 PyPI 上没有、在 Visual Studio 里有，两边无法同时精确满足，所以钉线而非钉点（版本不符直接 FAIL，不会悄悄给绿），见 workflow §3.2。
- **CI 未覆盖**：界面与下载链路仍属手工回归。

---

## 7. 模块交互方式总结（一图概括）

```
        ┌──────────────────────── 契约层 sdk/ToolBoxPlugin.h ───────────────────────┐
        │   ToolMeta  ToolSettings  IToolPlugin  IToolPage  (IID 版本化, ABI 锁定)   │
        └───────────────▲───────────────────────────────▲───────────────────────────┘
                        │ qobject_cast / 虚调用          │ qobject_cast / 虚调用
            ┌───────────┴──────────┐          ┌─────────┴──────────────────────┐
            │   外壳层 app/          │          │  工具层 plugins/<x>/           │
            │  MainWindow(View)      │          │  <X>Plugin(meta+createPage)   │
            │   ├─ ToolRegistry      │          │   └─ <X>Page(实现 IToolPage)  │
            │   │   (QPluginLoader)  │          │  <x>_core (纯逻辑, 可单测)     │
            │   └─ core/ToolCatalog  │          │                               │
            │   └─ core/LanguageChoice               │                           │
            └───────────┬──────────┘          └─────────▲──────────────────────┘
                        │ 信号槽仅限层内                  │ 信号槽仅限层内
                        ▼                                ▼
              QWidget/导航/StackedWidget         QPushButton/QProcess/QNetworkAccessManager

跨层通信 = 契约多态（虚函数 + qobject_cast），不是信号槽。
层内通信 = Qt 新式信号槽（lambda 带 context，重入用守卫/事件循环推迟）。
```

**一句话**：外壳通过 SDK 契约以「多态 + 虚调用」装载并托管插件页面，外壳与插件零信号槽耦合；信号槽只在各层内部（MainWindow 的导航/搜索、VideoDlPage 的进程/网络驱动）使用，并严格遵守新式语法、context 对象与重入防护。构建以 CMake target-based + 顶层统一输出与打包，质量由编译器/脚本/CI 三道防线保证。
