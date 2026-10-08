# 工具箱 · 架构设计

> 状态：生效中 · 最后修订：2026-10-01
>
> **本文档是架构的唯一权威来源。** 代码与本文档不一致时，以本文档为准，按
> [workflow.md](./workflow.md) 的「先改文档、后改代码」流程处理。
> 任何**有意**偏离本文档的实现，必须登记到文末的「偏差台账」，不允许悄悄不遵守。
>
> 编码风格与命名规则见 [coding-standards.md](./coding-standards.md)，
> 构建、测试、发布与文档变更流程见 [workflow.md](./workflow.md)。

## 1. 项目定位

插件式桌面工具箱。主程序只负责「外壳」：导航、页面容器、搜索、收藏、配置存取；
每个具体工具都是一个独立的 DLL 插件，放进 `<exe 目录>/tools/` 即可生效，
**新增工具不需要修改外壳代码**。这是本项目的核心设计约束，任何改动都不能破坏它。

技术栈固定为 **Qt 6 Widgets + C++17 + CMake + MSVC 2022 x64**。
本项目属于「传统 Widgets 桌面项目」，采用 **MVP + 分层**架构；
不采用 QML/MVVM（无触屏与属性绑定需求），也不采用嵌入式简化三层（不是资源受限环境）。

## 2. 目录与分层

```
tools-box/
├── CMakeLists.txt          顶层：统一 Qt 依赖、编译选项、输出布局，显式列出各子目录
├── CMakePresets.json       唯一构建入口（MSVC 2022 x64 / Qt 6.12.0 / Ninja Multi-Config）
├── docs/                   本目录：架构、规范、流程
├── scripts/verify/         开发期手工验证脚本（powershell）
├── cmake/                  CMake 公共片段：`toolbox_add_test()` 等全局函数
├── sdk/                    契约层：纯头文件 INTERFACE 库，无二进制
│   └── ToolBoxPlugin.h     唯一契约：ToolMeta / ToolSettings / IToolPage / IToolPlugin
├── app/                    外壳层：主程序
│   ├── main.cpp            应用元信息（组织名/应用名/版本）与入口
│   ├── core/               外壳的纯逻辑（无 QWidget 依赖），编成静态库 ToolBoxCore
│   ├── MainWindow.*        窗口装配、导航渲染、配置持久化
│   └── ToolRegistry.*      插件扫描与装载（与 MainWindow 一起编成静态库 ToolBoxApp）
├── plugins/<工具名>/        工具层：一个工具 = 一个 MODULE 库（DLL）
│   ├── core/               该工具的纯逻辑（无 QWidget 依赖），编成 <工具名>_core
│   ├── tests/              该工具自己的用例（见 §10 D3；全部用例都住在模块里）
│   └── *Orchestration*     编排层（可选）：把外部事件翻译成界面信号。
│                           编成 <工具名>_orch，外部通道经接口注入，故可测。
│                           详见 §3.1
（顶层 `tests/` 目录已随 D3 收官删除 —— 全仓 22 个用例都在各自模块的 `tests/` 下。）
```

`core/` 是约定的名字：**看到它就知道这里不放界面**。工具如果还没有独立逻辑，可以
不建这个目录，但一旦出现解析、转换、过滤这类可脱离界面表达的逻辑，就必须落到这里
（判定标准见 §3）。

三个层次的角色固定：

| 层 | 职责 | 不允许做的事 |
| --- | --- | --- |
| `sdk/` | 定义主程序与插件之间的契约 | 依赖 `app/` 或 `plugins/`；包含任何实现代码 |
| `app/` | 装载插件、装配外壳、托管工具页面与配置 | include 任何具体插件的头文件；知道任何工具的具体行为 |
| `plugins/` | 实现单个工具 | 依赖 `app/` 的任何头文件；互相依赖；直接访问其它插件的数据 |

**依赖方向不可违反**：`plugins → sdk ← app`。插件只能通过 `sdk/ToolBoxPlugin.h`
认识外壳，外壳只能通过 `IToolPlugin` / `IToolPage` 认识插件。

`app/` 内部也分了边界，测试按同一刀切开：`ToolBoxCore` 是 `core/` 的静态库且
**只链接 `Qt6::Core`**，`ToolBoxApp` 是 `MainWindow.*` + `ToolRegistry.*` 的静态库
（链接 `ToolBox::Sdk` + `ToolBoxCore` + `Qt6::Widgets`），可执行文件 `ToolBox` 里
只剩 `main.cpp`。依赖链是 `ToolBox → ToolBoxApp → {Sdk, ToolBoxCore, Qt6::Widgets}`。
「`ToolBoxCore` 只链 `Qt6::Core`」是条硬约束，也是「`core/` 里不许出现界面」这条规矩的
**编译器级**保证：往 `core/` 里 include 任何界面头文件会直接编译失败（§10 D1 验收标准 1）。

## 3. MVP 落地约定

分层的价值在于「UI 与业务解耦」，落到可执行的判断标准上只有一条：

> **凡是能用「不依赖 QWidget」的方式表达的逻辑，必须放在不依赖 QWidget 的类里。**

理由是这类代码可以被单元测试覆盖，而挂在窗口上的代码只能靠手工点。具体切分：

| 职责 | 归属 | 判定特征 |
| --- | --- | --- |
| 控件创建、布局、样式、按钮可用状态 | View（`QWidget` 派生） | 需要 `QWidget` 才能表达 |
| 只做展示与转发：把用户操作转成对 Model 的调用，把 Model 的变化刷到界面 | View | 函数体应该很短，不含分支密集的算法 |
| 导航模型、搜索过滤、收藏/最近使用的增删与去重、失效项剔除 | Model（无 UI 依赖） | 输入输出都是数据，可脱离窗口构造 |
| 配置的读写编排、插件装载 | Model / 服务 | 只用 `QSettings`、`QPluginLoader` 等非界面 API |
| 领域算法（cookie 规范化、地址提取、输出解析、直链解析） | 独立服务类或文件内自由函数 | 纯函数优先，无状态其次 |

**历史反例（P1 已修正，见偏差台账 9.1 / 9.2）**：把过滤算法写在 `MainWindow` 里、
把下载进度解析写在页面类里，都是把 Model 塞进了 View。现在这两处分别落在
`app/core/ToolCatalog.*` 与 `plugins/videodl/core/`，并且都有测试兜底。

### 3.1 编排层（`_orch`）：不是 core，但要能测

有一类代码两边都不沾：它不碰任何控件，却持有 `tr()` 文案（把 Model 的变化刷成
一句话给用户看），按上面那张表属于 View；可它的价值又全在**决策**上 —— 哪一行输出
对应哪个阶段、取消与重试撞车时谁赢。放 `core/` 不合适（那是 View 的活），挂回页面
则彻底失去可测性。

这类代码单独编成 `<工具名>_orch` 静态库，并且**外部通道一律经接口注入**：

| 外部通道 | 接口 | 真的实现 |
| --- | --- | --- |
| 子进程 | `IChildProcess` | `RealChildProcess` |
| 网络 | `IEngineTransport` | `NetworkEngineTransport` |

于是测试可以换成假实现，喂预置字节与数据、断言信号 —— **既不启动子进程也不发出
网络请求**，`workflow.md §5` 那条约定依然成立。这不是绕开约定：约定要防的是
「测试依赖外部世界因而脆弱」，注入恰恰让测试不再依赖外部世界。

注意边界：**注入换不来真实路径的覆盖**。`RealChildProcess` / `NetworkEngineTransport`
本身并没有被单元测试覆盖，它们仍靠手工回归（workflow §8）。注入换来的是决策分支能
被确定地走到，尤其是那些真机上靠运气才撞得到的（合并阶段要切不确定态、重试等待
期间点取消）。

`videodl_orch` 用 `qt_add_library` 而不是 `add_library`：这些类都带 `Q_OBJECT`，
普通 `add_library` 不跑 moc 会缺虚表符号。

## 4. 插件契约

契约定义在 [ToolBoxPlugin.h](../sdk/ToolBoxPlugin.h)，是**唯一**的跨模块接口。

### 4.1 工具标识

- `ToolMeta::id` 必须匹配 `^[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*)+$`，形如 `分类.工具名`。
- 现有取值：`text.base64`、`dev.json-format`、`media.image-watermark`、`media.video-download`。
- **id 一旦随版本发布，永久不可更改**——它同时是配置键前缀、收藏/最近使用的持久化
  依据（`ui/favorites`、`ui/recent`）。改名等于让所有用户丢配置。
- 页面里所有持久化键都以 id 做前缀隔离，插件之间不会串键。

### 4.2 版本与 ABI

- 插件接口 IID 已版本化：`com.toolbox.ToolBox/IToolPlugin/1.0`。
- **构建环境指纹（`abi`）**：每个插件的静态元数据里带一个 `abi` 字段，形如
  `6.12.0-MSVC-144`（Qt 版本 + 编译器 + 工具集）。宿主在 `load()` **之前**比对，
  不一致就跳过并记录错误。
  - 为什么还需要它：IID 只能挡「接口版本不对」，挡不住「接口版本对、却是用另一套
    Qt / 编译器构建」的 DLL —— 那种能被 `QPluginLoader` 加载进来，然后因为二进制
    不兼容在宿主进程里崩掉。只有指纹能在不加载的前提下识别它。
  - 字段由构建注入，不手写：`metadata.json` 已改成 `metadata.json.in`，经
    `configure_file` 注入（变量 `TOOLBOX_PLUGIN_ABI`）；宿主侧拿的是**同一个变量**
    经 `TOOLBOX_HOST_ABI` 编进 `ToolBoxCore`。两边同源，因此不可能漂移 ——
    手写一份的后果是每个插件都被自家门禁拒掉。
  - 判定规则在 [app/core/PluginScanPolicy.*](../app/core/PluginScanPolicy.h)
    的 `evaluatePluginFields()`（可脱离 `QPluginLoader` 单测）；取 JSON 走
    [ToolBoxPlugin.h](../sdk/ToolBoxPlugin.h) 的 `pluginMetaObject()`，
    「`MetaData` → `toolbox`」这个形状只有一处定义。
- `ToolMeta` 按值跨 DLL 传递，**只允许在末尾追加字段**，不允许改类型、不允许重排。
- 修改 `IToolPlugin` / `IToolPage` 的已有签名 = 破坏 ABI，必须：
  1. 升 IID 版本号；
  2. 重新编译**全部**插件（主程序与插件必须同一套 Qt、同一编译器、同一 C++ 标准）；
  3. 更新本节与「偏差台账」。
- `ToolMeta::version` 是插件自己的版本号，与主程序版本无关，各插件独立演进。

### 4.3 meta() 的调用约定

- **`meta()` 必须无副作用，且同一次装载期间返回值保持稳定。** 主程序在装载时调用
  一次，把结果缓存在 `ToolRegistry::Entry::meta` 里，随后的排序、导航、状态提示都读
  这份缓存（见偏差 9.8）。
- 推论：返回值**不得依赖会在本次装载期间变化的状态**（例如「当前选中的工具」
  「刚刚下载完内核」）。真需要那种动态描述，得换 id 走一次完整的重新装载，而不是
  靠重新调用 `meta()`。
- 缓存的原因是 `meta()` 是跨 DLL 的虚调用，而排序比较器会被调用 O(n log n) 次。
- `meta()` 里的 `name` / `category` / `description` 是**用户可见文案，用 `tr()` 包裹**
  （否则英文界面下导航永远是中文）。这不构成上面说的「动态状态」：界面语言在
  `main.cpp` 里**启动时定一次、运行期不切换**（§6），同一次装载期间译者不变，
  `tr()` 的结果自然稳定。换句话说，**「改语言要重启」这个取舍正是 `meta()` 能被
  缓存的前提** —— 若哪天要做运行期切换，本节这条契约与 ToolRegistry 的缓存都得
  重新设计。

### 4.4 生命周期约束

- `createPage()` 每次调用返回**新实例**，所有权立即归外壳；外壳负责销毁。
- 页面若要持久化，实现 `IToolPage` 并声明 `Q_INTERFACES`；不实现也不会出问题。
- **卸载顺序不可颠倒**：保存状态 → 销毁全部页面 → 卸载 DLL。
  插件一旦卸载，页面里的虚表指针即悬空。该顺序由 `MainWindow::reloadTools()`
  与 `ToolRegistry::rescan()` 共同保证，见 [MainWindow.cpp](../app/MainWindow.cpp)、
  [ToolRegistry.h](../app/ToolRegistry.h) 的类注释。
- 页面析构时必须自己收干净正在运行的子进程/网络请求（`QProcess` 未结束就析构会
  报 `Destroyed while process is still running`）。

## 5. 线程与阻塞

- **界面只有一个线程**。所有界面更新都在 GUI 线程完成。
- 耗时 I/O 走异步：网络用 `QNetworkAccessManager`，外部程序用 `QProcess` 的信号驱动。
- 禁止在 GUI 线程做无超时的阻塞等待。允许的例外必须同时满足：
  1. 显式超时且不超过 5000 ms；
  2. 位于用户主动触发的路径或收尾路径；
  3. 就近写明「为什么这里可以阻塞」的注释。
  现有实例：只有析构时收尾子进程的那一次 `waitForFinished(2000)`。抖音渲染启动
  曾经也有一个 `waitForStarted(5000)`，已改回异步：那次阻塞既卡界面，又让
  「`waitForStarted` 返回 false」与「`errorOccurred(FailedToStart)`」两条路径
  同时报失败，日志里出现两条互相矛盾的原因。启动结果现在统一由
  `onRenderFinished()` 一处判定。

## 6. 状态与持久化

- 组织名/应用名固定为 `ToolBox`（在 `main.cpp` 中于任何 `QSettings` 构造之前设置），
  存储后端为注册表。
- 键的命名空间划分：
  - `ui/*` —— 外壳专用（`ui/lastToolId`、`ui/favorites`、`ui/recent`、`ui/language`），
    插件不得读写；
  - `plugin/<工具 id>/*` —— 该工具专属，由 `ToolSettings` 自动加前缀，插件只写裸键名。
- `ui/language` 的取值：**空串（或不存在）= 跟随系统地区**；非空按语言代码强制
  （只认 `en*` / `zh*`，认不出来的值退回跟随系统）。**在启动时读一次，运行期不切换**
  —— 改语言要重启。解析逻辑在 `app/core/LanguageChoice.*`，见
  [workflow.md §3.3](./workflow.md#33-界面语言与翻译)。
- 插件**不允许**直接构造 `QSettings`，一律通过 `toolbox::ToolSettings`。
- 持久化数据必须能在工具消失后自愈：外壳在每次装载后剔除指向已不存在工具的 id。
- 缓存/临时文件只放系统临时目录，且必须在任务结束与页面析构时清理；
  含登录凭据的文件（如 cookies 副本）不得长期驻留。
- 唯一的持久化**文件**是结构化日志：`<AppDataLocation>/ToolBox/toolbox.log`，
  由 `app/main.cpp` 在任何业务日志之前安装
  （[app/core/Logger.*](../app/core/Logger.h)）。它按大小轮转、份数有上限
  （默认单文件 5 MiB、留 3 份），最低级别可被 `TOOLBOX_LOG_LEVEL` 临时覆盖 ——
  属于「有界、可预期」的落盘，因此不受上一条「临时文件只放临时目录」约束；
  除它之外，程序不留任何持久文件。
- **一次跨越多条日志的操作必须开追踪 ID**（`toolbox::LogTrace` 或
  `Logger::beginTrace()`，见 [Logger.h](../app/core/Logger.h) 的注释）：同一 id 会
  出现在本次操作的每一行日志里，`grep <id>` 即可把整条链路捞出来 —— 否则不同操作的
  日志在文件里是交错的，只能靠时间戳猜归属。典型位置：一次下载、一次内核安装、
  一次抖音渲染。**用 `LogTrace` 守卫而不是手写 begin/end**：中途 return 漏掉
  endTrace 会让后续所有无关日志都挂着一个早就结束的 id，追踪反而变成误导。
  该 id 是**线程局部**的，跨线程不自动继承（有需要由调用方传过去再 `setTraceId()`）。

## 7. 错误处理与用户反馈

- 插件加载失败**不能**阻断外壳启动：错误收集进 `ToolRegistry::errors()`，
  在首页列出文件名与原因。
- 用户可见的失败必须同时给「现象 + 下一步动作」（如「未找到 yt-dlp —— 点下面的
  按钮下载，或手动指定路径」），不允许只报 `false`/空界面。
- 长时间任务必须有一行文字说明当前阶段（解析、下载、合并、转码、解压），
  仅靠百分比进度条不足以判断是否卡死。
- 对外部输入的解析（第三方输出、网页、cookie 文件）一律按「可能不合规」处理：
  能修正就修正并记录，不能修正就丢弃并提示，不允许抛异常或静默失败。

## 8. 构建与输出布局

- 输出布局由顶层 `CMakeLists.txt` 的 `TOOLBOX_BIN_DIR` 统一决定：
  `bin/<Config>/ToolBox.exe` 与 `bin/<Config>/tools/*.dll`。
  插件 DLL 输出目录由各插件 `CMakeLists.txt` 指向 `TOOLBOX_BIN_DIR/tools`。
- 插件外部依赖的内核程序（如 yt-dlp / ffmpeg）落在 `<exe 目录>/tools/bin/`，
  与插件 DLL 同处一层，保证整个 `bin/<Config>/` 拷走即可运行。
- 运行时可执行文件的查找顺序固定为：**手动指定 → `<exe 目录>/tools/bin/` → PATH →
  共享内核目录**（2026-10-02 加的最后一级兜底，见下）。
- **共享内核目录**是用户配置的一个目录（应用级配置键 `ui/sharedEngineDir`，
  常量在 SDK 里：`toolbox::kSharedEngineDirKey`），让同一台机器上的多个工具
  共用一份 yt-dlp / ffmpeg，而不是各下载一份。它**排在最后**：前三级都是
  「本机本来就有的」，用户特意指出来的目录不该抢在前面；清空它则行为完全回到从前。
  之所以不做成「强制公共目录」：那会毁掉「解压即用」的便携形态（zip 换台机器
  就残废）、撞上写权限（`Program Files` 不可写），并让多版本同名冲突无解。
- **交付形态就是 `bin/<Config>/` 这一个目录**，由顶层的 `install()`（整目录安装）
  与 CPack 打成 zip 发出。因此 `install()` 规则不允许出现在子目录里 ——
  一旦有人往插件目录里加 `install(TARGETS)`，交付内容就会分裂成两处描述。
  发布流程见 [workflow.md §9](./workflow.md#9-发布)。

## 9. 偏差台账

> 登记规则：**任何有意不遵守本规范的地方都必须在此登记**，写明原因、决定与计划。
> 没有登记的不一致，一律视为待修复的缺陷。发版前必须过一遍本表。

| # | 偏差 | 位置 | 原因 | 决定 | 计划 |
| --- | --- | --- | --- | --- | --- |
| 9.1 | 导航的领域逻辑曾写在窗口里 | [MainWindow.cpp](../app/MainWindow.cpp) | 功能逐步叠加，规模尚小 | **已收敛（P1）**：过滤与列表维护规则已抽到 [ToolCatalog.cpp](../app/core/ToolCatalog.cpp)，窗口只留控件装配与渲染 | 剩余的 `ui/NavPanel`、`IconFactory` 表现层拆分主动放弃，理由见 §10 D1 |
| 9.2 | 单个页面类承担界面、进程、网络、解压、解析、平台适配 | [videodl/VideoDlPlugin.cpp](../plugins/videodl/VideoDlPlugin.cpp) | 抖音适配与内核下载为后期追加 | **已收敛（P1，2026-10-02）**：解析规则在 `plugins/videodl/core/`（可单测），内核下载在 `EngineFetcher`，下载进程在 `DownloadRunner`，抖音渲染在 `DouyinResolver`，命令行构造在 `core/DownloadArgs`；页面 678 行，**不再持有任何 `QProcess` / `QNetworkAccessManager`** | 文件规模由 `scripts/verify/verify_filesize.ps1` 按 800 行上限盯住，超了就 FAIL（本次拆分即由它触发） |
| 9.3 | ~~无自动化测试，纯逻辑靠手工脚本验证~~ | 全项目 | 一直以手工验证推进 | **已消除（P1）**：`tests/` 下 22 个 Qt Test 目标（含外壳装配 / 跨 DLL 的集成用例，以及「对**真实产物**启动并点击」的 GUI 冒烟 `verify_shell.ps1`），双配置 `ctest` 全绿 | 新增 `*/core/` 模块必须同步补用例（workflow §5）。`verify_coretest.ps1` 机械检查，且自 2026-10-07 起从文件级加到**函数级**（读符号表比对「导出的 core 函数」与「测试实际引用的符号」），并当场逼出一个真缺口：`normalizedCookiesPath()` 曾被生产代码用了三处而测试零覆盖 |
| 9.4 | ~~验证脚本混在可再生成的 `build/` 目录内~~ | `build/*.ps1` | 顺手放置 | **已消除（P0）**：脚本迁至 `scripts/verify/`，固定样本在 `scripts/verify/fixtures/` | 脚本运行时的截图/下载产物仍落在 `build/` —— 那些是可再生成物，属于正确位置 |
| 9.5 | ~~版本号硬编码两处~~ | [CMakeLists.txt](../CMakeLists.txt)、[main.cpp](../app/main.cpp) | — | **已消除（P2）**：版本号只留顶层 `project(... VERSION ...)`，由 `app/CMakeLists.txt` 的 `TOOLBOX_VERSION` 编译定义传给 `main.cpp` | 无 |
| 9.6 | ~~部署使用 `--no-translations`，Qt 自带对话框按钮为英文~~ | [app/CMakeLists.txt](../app/CMakeLists.txt)、[main.cpp](../app/main.cpp) | 早期为避免拷贝多余文件 | **已消除（P2）**：deploy 改为 `--translations zh_CN`，并在 `main.cpp` 里安装 `QTranslator`（只拷文件不装翻译器无效） | 无 |
| 9.7 | 插件 `CMakeLists.txt` 样板重复 | `plugins/*/CMakeLists.txt` | 复制目录即建新插件的模板 | 接受 | 出现第 5 个插件时抽取 `toolbox_add_plugin()` |
| 9.8 | ~~排序比较器中反复调用 `plugin->meta()`~~ | [ToolRegistry.cpp](../app/ToolRegistry.cpp) | — | **已消除（P2）**：装载时取一次存进 `Entry::meta`，比较器只读缓存；`meta()` 的无副作用契约见 §4.3 | 无 |
| ~~9.9~~ | ~~静态检查未接入 CI~~ | 根目录 `.clang-format` / `.clang-tidy`、`.git-blame-ignore-revs`、`scripts/verify/verify_format.ps1`、`.github/workflows/ci.yml` | 格式曾无法强制：归一化前 `.clang-format` 会让 15 个文件 / 328 行发生改动（clang-format 没有「保留手工换行」的选项，且只有一个全局 `AfterEnum` 开关） | **已消除（P3）**：格式在 73b1e4a 一次性归一化（字符多重集比对确认只动空格与换行），此后由 `verify_format.ps1` 强制、提交登记进 `.git-blame-ignore-revs`；CI 已接入 GitHub，push / PR 时在干净机器上跑四个快检 + 双配置构建 + 双配置 `ctest` + 命名检查 + 打包。命名检查（`verify_naming.ps1`）的工具版本钉在 **LLVM 22.1 这条线**上 —— CI 装 PyPI 的 `clang-tidy==22.1.8`，本机 VS 自带的 22.1.3 同样通过，版本不在线上脚本直接 FAIL；钉发行线而不是精确版本的理由见 [workflow.md §3.2](./workflow.md#32-命名检查clang-tidy) | 无。CI 覆盖不到的界面与下载链路仍属手工回归（[workflow.md §11](./workflow.md#11-持续集成ci)） |
| 9.11 | Qt 自带控件的翻译在 Qt 6.12 上失效 —— 消息框按钮回到 `OK`，而工具自己的中文文案完全正常 | [app/main.cpp](../app/main.cpp)、[app/core/LanguageChoice.cpp](../app/core/LanguageChoice.cpp) | 9.6 的修复按 **Qt 5 的文件名**（`qt_<locale>.qm`）加载，而 Qt 6 把词条搬到了 `qtbase_<locale>.qm`，旧文件退化成一个 **99 字节空壳**（实测 147222 vs 99）；本机 Qt 又装在非标准路径 `C:\Qt6.12\`，`QLibraryInfo::path(TranslationsPath)` 指向的编译期前缀 `C:/Qt/6.12.0/...` 并不存在，回退路径也落空 | **已消除（P2）**：加载前缀改为 `qtbase`（保留 `qt` 回退）；`LanguageChoice` 在配置只写语言（`zh`）时补上默认地区；`verify_shell.ps1` 自己设并复原 `ui/language`，不再依赖调用方 | 守住它的是 `verify_shell.ps1` 里那条「对话框按钮是否被本地化」的断言 —— 而**这条断言此前从未在本地跑过**，所以退化一直没人发现（详见 [workflow.md §3.3](./workflow.md#33-界面语言与翻译)） |
| 9.10 | ~~无 `install()` / CPack 打包规则，交付靠手工拷贝 `bin/`~~ | 顶层 [CMakeLists.txt](../CMakeLists.txt) | 交付频次低 | **已消除（P3）**：顶层整目录 `install()` + CPack 出 zip，版本号仍只有 `project(... VERSION ...)` 一个来源；两条 `install(CODE)` 保护会拦下「打错配置」与「忘了 deploy」，实测都会报错停下 | 发布流程见 [workflow.md §9](./workflow.md#9-发布)；`verify_shell.ps1 -Exe` 用于验收解压后的产物 |
| 9.12 | 运行时内核（yt-dlp / ffmpeg）从 GitHub `releases/latest` 下载，**未钉版本、无哈希校验** | [VideoDlPlugin.cpp](../plugins/videodl/VideoDlPlugin.cpp)（URL 常量）、[EngineFetcher.cpp](../plugins/videodl/EngineFetcher.cpp)（下载与校验） | yt-dlp 的价值就在「跟着站点对抗跑」，钉旧版本反而让下载功能随上游站点改版静默失效；release 更新频繁，手维护哈希表的成本与漂移风险都高 | **接受不钉版本（P1，2026-10-02）**；但补上**最小完整性校验**——[core/EngineCheck](../plugins/videodl/core/EngineCheck.h) 查文件头魔数（PE / Zip）+ 大小下限（8 MB / 32 MB），在落地环节拦住「下载到 HTML 错误页 / 严重截断文件」这类最常见的损坏。这不是安全边界，是可用性闸门 | 校验规则由 `tst_enginecheck` 盯住；若上游提供稳定的校验和清单，再升级为哈希校验 |
| 9.13 | 引擎安装的解压步骤依赖系统自带 `powershell.exe`（`Expand-Archive`） | [EngineFetcher.cpp](../plugins/videodl/EngineFetcher.cpp)（`extractFfmpeg`） | 当初为「不引第三方库」选了系统自带解压 | **已知局限（P2，2026-10-02 实测）**：受限环境（企业安全策略 / 开发沙箱）会拦子进程，界面只报「ffmpeg 安装失败：无法解压。」而不说原因；当晚 ffmpeg 压缩包已下载并通过完整性校验，卡在解压 | 治本方案是改用内嵌的 minizip（单文件、MIT），彻底去掉对 `powershell.exe` 的依赖；未做，等大帅排期 |
| 9.14 | 「行覆盖率」在本机不可得：MSVC 的 `/FUCOVERAGE` 与随 VS 附带的 `llvm-cov` **格式不兼容** | [verify_coretest.ps1](../scripts/verify/verify_coretest.ps1) 头注、`.workbuddy/g3-coverage-feasibility-2026-10-07.md` | 最初假定「VS 自带 llvm-cov，只差一个开关」 | **已确认为死路（2026-10-07 四组组合实测）**：`/FUCOVERAGE` 编译链接成功、exe 正常退出，但**四种组合都没产出任何 profile**（含 `LLVM_PROFILE_FILE`、把 `PROFILE` 传给 cl 与 link.exe）。根因：`/FUCOVERAGE` 写的是 MSVC 自家的旧式 `.cov` 二进制，而 `llvm-cov` 只认 clang 的 `.profraw`/`.profdata` | 替代路径三条：①**已采用**——`verify_coretest` 从文件级加到**函数级**（读符号表比对「导出的 core 函数」与「测试 obj 实际引用的符号」，零解析零误报，已抓出一个真缺口）；②OpenCppCoverage（吃 MSVC 原生数据，代价是第三方二进制进 CI）；③换 `clang-cl` 工具集（须重验 Qt 兼容 / ccache / CI 矩阵） |
| 9.15 | `verify_coretest.ps1` 的输入从「源码」改成了「构建产物」 | [verify_coretest.ps1](../scripts/verify/verify_coretest.ps1)、[ci.yml](../.github/workflows/ci.yml) | 函数级覆盖只能问编译器要答案（见 9.14） | **有意接受**：一个依赖构建的门禁，若在没构建时**报 SKIP 就等于假绿**（「因为没东西可看而通过」）。现在输入缺失一律 **FAIL**；CI 步骤相应从「快检段」移到「Build Debug」之后；`run_all` 与 `pre_commit` 里它也不再被当作免构建检查（`pre_commit` 每次提交都要求先构建，会训练大家用 `--no-verify`） | 守住它的是**反向验证**：移走 `tst_base64` 的 Debug obj，门禁必须精确报出 `decodeBase64`/`encodeBase64` 未被引用并 exit 1。另注意**只扫 Debug**：曾因 Debug+Release 同扫，注入的 Debug obj 被 Release 顶替、门禁仍假绿 |

| 9.16 | `plugins/imgwatermark/core/DctBasis.h` 是**含定义的头文件**（定义放在匿名 namespace 里），仅由 `Stego.cpp` 一个 TU 包含 | [DctBasis.h](../plugins/imgwatermark/core/DctBasis.h) | 拆文件是为了把 `Stego.cpp` 压回 600 行门禁以内（543 行，格式化后），而 DCT 基底设施（正逆变换、块读写、比特调制）恰好是清晰的独立边界 | **有意接受（2026-10-07）**：单消费者下匿名 namespace 语义正确、符号不外泄。风险是**日后出现第二个消费者时，每个 TU 各留一份副本、静默膨胀**；头文件注释已写明「若出现第二个消费者需改成 inline 并去掉 namespace」 | `verify_coretest` 只扫 `core/*.cpp`，故本头文件**无需**被测试 include —— 实测一旦 include，clang-tidy 立刻报它的 6 个函数在该 TU 里 unused |

| 9.17 | 数字水印的「自动水印文本」（文件名 + 尺寸 + 体积 + 时间的拼接）写在页面 .cpp 里，**无法单测** | [WatermarkText.cpp](../plugins/imgwatermark/WatermarkText.cpp) | 它与界面无关、却是本工具唯一的溯源信息生成处；而 tools-box 对「可测性缺口」的态度是「能挪进 core 就挪」（architecture §3）| **已消除（P3，2026-10-07）**：抽成 `WatermarkText.h/.cpp`，签名改为显式参数（原先收 `QFileInfo`，测试就得造临时文件），由 `tst_stego` 的 `autoWatermarkTextCarriesTraceableFields()` 与 `outputFormatsCoverLosslessAndLossy()` 盯住 | 顺带把 `ImgWatermarkPlugin.cpp` 从 631 行降到 578 行，回到 `verify_filesize.ps1` 的 600 行门禁以内（不必登记豁免） |
| ~~9.18~~ | ~~`toolbox_add_test()` 按**两个位置**找测试源码~~ | [cmake/ToolBoxTest.cmake](../cmake/ToolBoxTest.cmake) | D3 分批推进期间，`plugins/videodl/tests/`（已搬）与顶层 `tests/`（未搬）**并存**，函数必须两处都认 | **已消除（P2，2026-10-08 当日收官）**：其余模块当天全部跟进搬迁，第二条查找路径已删，现在**只认** `<模块>/tests/<name>.cpp`，找不到即配置期 FATAL_ERROR —— 它第一次上岗就拦下了 `tests/tests/` 这个路径拼接错误。原注销判据（顶层 `tests/` 只剩不属单个模块的用例）更进一步：连 `tst_integration` 都归属了 `app/tests/`，顶层 `tests/` 目录整个消失 | 无 |

## 10. 目标架构与迁移计划

**不推倒重来。** 迁移与功能改动绑定：只有当某个文件因新需求被改动时，才顺带完成它
对应的拆分。这样每一步都有验收标准，且不需要为重构单独排期。

### D1 · 外壳拆分（触发条件：下次改动 `MainWindow` 相关文件）

最初的拆分设想（**注意：这是设想，不是现状** —— 现状见本节末尾）：

```
app/
├── main.cpp
├── core/                       无 QWidget 依赖，可单测
│   ├── ToolRegistry.*          ← 设想放在这里，实际放不进来，见下
│   ├── ToolCatalog.*           工具条目模型 + 搜索过滤 + 收藏/最近使用
│   └── IconFactory.*           无图标时的占位图标生成
└── ui/
    ├── MainWindow.*            仅装配与转发
    └── NavPanel.*              导航列表的构建与分区渲染
```

**现状（P3 订正）**：`app/` 下只有 `main.cpp`、`MainWindow.*`、`ToolRegistry.*` 和
`core/ToolCatalog.*`；`ui/` 目录**从未创建**，`IconFactory` 也没有。

其中 `ToolRegistry.*` **进不了 `core/`，不是还没做，而是做不到**：它 include 了
`ToolBoxPlugin.h`，而这份 SDK 契约头文件本身 include 了 `<QWidget>` 与 `<QIcon>`
（`IToolPage::createPage()` 返回 `QWidget *`，`ToolMeta::icon` 是 `QIcon`）。任何处理
`ToolMeta` 的代码都因此需要 QtGui/Widgets，而 `ToolBoxCore` 只链接 `Qt6::Core`
（正是下面验收标准 1 所依赖的不变量）。放进去会直接编译失败 —— 这是**契约层决定的
边界**，不是拆分力度不够。

推论：外壳里能进 `core/` 的只有「不碰 `ToolMeta`」的逻辑，`ToolCatalog` 正是如此
（它吃的是拍平后的 `CatalogEntry`，与 `QIcon`/`QWidget` 无关）。将来若想让
`ToolRegistry` 可单测，要动的是**契约**（例如让 `ToolMeta` 不携带 `QIcon`），
而按 §4.2 那属于破坏 ABI 的改动，必须升 IID 版本号并重编全部插件。

验收标准：
1. `core/` 下的文件不 include 任何 `QtWidgets` 头文件。**这一条不需要 grep 兜底**：
   `ToolBoxCore` / `videodl_core` 都是静态库且只链接 `Qt6::Core`，包含界面类头文件会
   直接编译失败；反向依赖（`plugins` ↔ `app` 互相 include）另由
   `scripts/verify/verify_conventions.ps1` 检查；
2. 搜索过滤、收藏/最近使用的增删与去重有 Qt Test 用例；
3. 手工验证脚本 `scripts/verify/verify_recent.ps1` 全通过。

**进度（P1：核心部分已完成）**：[ToolCatalog.*](../app/core/ToolCatalog.h) 已落地，
`MainWindow` 只保留控件装配与渲染；`tst_toolcatalog` 覆盖过滤器与列表维护，
`verify_recent.ps1` 实测通过。验收标准 1–3 均已满足（标准 1 对 `core/` 的约束成立，
因为 `core/` 里只有不碰 `ToolMeta` 的 `ToolCatalog`）。

**决定：`ui/MainWindow`、`ui/NavPanel`、`IconFactory` 这三项不再做。** 它们是纯表现层
搬运：`MainWindow` 剩下的代码就是「建控件、按模型刷列表、转发信号」，再切出一个面板类
只会多一层无收益的转发；占位图标生成虽然无 QWidget 依赖，但它只被窗口调用一处，
抽出去换不来可测性。触发条件改为「出现第二个需要渲染导航分区的窗口时」。

### D2 · 视频下载插件拆分（触发条件：下次扩展该插件）

```
plugins/videodl/
├── VideoDlPlugin.*             插件入口、meta() 与页面（现状：VideoDlPage 也在这里，678 行）
├── DownloadRunner.*            跑 yt-dlp，输出行 → 进度/阶段/产物信号
├── DouyinResolver.*            借浏览器渲染取 video_id，拼播放直链
├── EngineFetcher.*             内核下载 / 断点续传 / 解压 / 取消
├── IChildProcess.*             子进程的注入点：接口 + RealChildProcess
├── IEngineTransport.*          网络传输的注入点：接口 + NetworkEngineTransport
└── core/                       无 QWidget 依赖，可单测
    ├── OutputParsing.*         输出解码、剥色、地址提取、进度/阶段/产物解析、
    │                           文件名消毒、Content-Range 解析、命令行脱敏
    ├── DownloadArgs.*          一次下载 → yt-dlp 参数的全部规则
    ├── DouyinSupport.*         站点判定、画质档位、DOM 字段提取
    ├── EngineLocator.*         内核定位顺序（手动 → 随程序目录 → PATH）
    └── CookieFile.*            cookie 规范化
```

`core/` 编成静态库 `videodl_core`（PUBLIC 暴露头文件目录），插件本体链接它；
页面侧只保留两件只有它才能做的事：把 `QCoreApplication` 接上（`engineDir` 适配器）
和 `tr()` 文案。

验收标准：
1. `VideoDlPage` 不再直接持有 `QProcess` / `QNetworkAccessManager`；
2. cookie 规范化、地址提取、进度解析、`video_id` 解析均有 Qt Test 用例；
3. 抖音与 B站 各完成一次真实下载回归。

**进度（P1：解析部分已完成）**：四个 `core/` 模块已抽出并接入测试，验收标准 2 满足。

**已完成（2026-10-02）**：`VideoDlPage` 现在**不持有任何 `QProcess` /
`QNetworkAccessManager`**，只做界面与信号接线，验收标准 1 **达成**。拆出来的是：

| 拆出的部分 | 负责什么 | 位置 |
| --- | --- | --- |
| `EngineFetcher` | 内核（yt-dlp / ffmpeg）下载、断点续传、解压、取消 | [EngineFetcher.h](../plugins/videodl/EngineFetcher.h) |
| `DownloadRunner` | 跑 yt-dlp、把输出行翻译成进度/阶段/产物信号 | [DownloadRunner.h](../plugins/videodl/DownloadRunner.h) |
| `DouyinResolver` | 借浏览器渲染取 `video_id` 并拼出播放直链 | [DouyinResolver.h](../plugins/videodl/DouyinResolver.h) |
| `buildYtDlpArgs` | 「一次下载翻译成什么命令行」的全部规则（纯逻辑，可单测） | [core/DownloadArgs.h](../plugins/videodl/core/DownloadArgs.h) |

触发这次拆分的是 `scripts/verify/verify_filesize.ps1`：页面先被撑到 1358 行、撞破
当时 1300 的豁免上限，**检查直接失败**。这正说明把「600 行要评估拆分」做成脚本是对的 ——
靠人记着，这条规则等于不存在。拆分后页面 678 行，豁免上限随之收紧到 800 行。

**触发条件已兑现**：原计划里那三个「各只有一处调用点、拆了也换不来可测性」的顾虑，
被 `verify_filesize.ps1` 的硬上限推翻了 —— 页面撞破上限后不改不行，而拆完发现收益比
预期大：`buildYtDlpArgs` 这批规则（画质档位、无 ffmpeg 降级、模板转义）**本来就该是
纯逻辑**，抽进 `core/` 之后第一次有了测试（`tst_downloadargs`），验收标准 3 的
真实下载回归仍属手工（见 workflow §8）。

### D3 · 模块内聚：测试跟着被测代码走（已完成，2026-10-08）

**起因**：本项目的分层是按**技术**切的（`sdk` / `app` / `plugins` / `tests`）。业务代码
早已按模块聚合（`plugins/<工具名>/` 自带 `core/` 与界面），唯独测试另起一层 —— 改一个
工具时，它的用例在仓根另一头，「改了 `core/` 忘了补测试」没有任何物理距离上的提醒。

**目标形态**：一个模块的测试放在 `<模块目录>/tests/` 下，由该模块自己的
`CMakeLists.txt` 用 `toolbox_add_test()` 注册。这是前端「模块化单体 / 垂直切片」里最
值得借的那一条 —— 内聚与就近，与语言无关。

**明确不做的那一半**：模块化单体的另一半是「单一部署单元」（模块间编译期链接、一个
二进制）。本项目是插件式**运行时**装载，插件 DLL 独立分发是 §1 的核心约束，那一半
**不予采纳** —— 这不是没做到，是取舍。同理，模块间的通信仍只有 `sdk/` 一条契约，
不引入通用模块通信总线（现在只有外壳一个消费者，那是过度设计）。

机制：

- `toolbox_add_test()` 由顶层 [cmake/ToolBoxTest.cmake](../cmake/ToolBoxTest.cmake)
  提供。定义在 `tests/` 下时它是目录局部函数，模块看不见 —— 提升成全局函数，模块才
  能自己管自己的测试。它把可执行文件统一落到 `build/tests/<Config>/`（不混进交付目录
  `bin/<Config>/`），并把 Qt 的 `bin` 前置进 `PATH`。
- 源码固定在 `<当前目录>/tests/<name>.cpp` —— 注册它的那个模块的 tests/ 子目录；
  找不到就**配置期失败**，静默跳过会让用例悄悄消失。顶层 `tests/` 目录因此**整个
  消失**：连 `tst_integration` 都归属了 `app/tests/`（它测的是外壳的装载机制，
  插件 DLL 只是它自己部署的测试夹具），不再存在「目录名已经叫 tests」的特例注册点
  —— 事实上正是这条配置期门禁在第一次配置时就拦下了 `tests/tests/` 这个路径拼接
  错误，它第一次上岗就抓到了一个真问题。
- 门禁脚本（`verify_coretest` / `verify_conventions` / `verify_translations`）已改为
  **按全仓匹配 `tst_*.cpp` 与任意 `tests/` 目录**，不再钉死 `tests/` 这一个路径。所以
  后续搬模块是纯移动、零脚本成本 —— 这一条正是本计划敢分批推进的前提。

**进度（2026-10-08 当日收官）**：`videodl` 试点（12 个用例）落地后，其余模块当天
全部跟进 —— app 5 个、sdk 1 个、base64 / jsonfmt / imgwatermark 各 1 个。顶层
`tests/` 目录已删除，全仓 22 个用例全部位于各自模块的 `tests/` 下，与「按技术分层」
的旧目录结构彻底告别。

验收标准：

1. 搬完的模块，`<模块>/tests/` 下的用例数与原本属于它的用例数一致，双配置 `ctest` 全绿；
2. `verify_coretest.ps1` 报出的「已覆盖 core 函数」数量不比搬迁前少 —— obj 挪了位置，
   函数级门禁不能因此漏判；
3. `scripts/verify/run_all.ps1` 全通过。

## 相关文档

- [coding-standards.md](./coding-standards.md) —— 命名、内存、信号槽等编码规则
- [workflow.md](./workflow.md) —— 构建、测试、加插件、发布与文档变更流程

以下为持续维护的记录与分析报告（记录某次评估或迁移，可追溯；规范一律以本文与上述两份为准）：

- [error_ledger.md](./error_ledger.md) —— **持续维护**的错误台账：症状隔得很远的顽固问题，每条带防止复发的手段
- [architecture-analysis-report.md](./architecture-analysis-report.md) —— 架构与代码结构分析（实现层速览）
- [best-practices-assessment.md](./best-practices-assessment.md) —— Qt 最佳实践符合性评估与落地记录
- [version-migration-audit.md](./version-migration-audit.md) —— Qt 6.12 版本迁移的深度完整性审计报告
