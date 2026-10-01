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
├── CMakePresets.json       唯一构建入口（MSVC 2022 x64 / Qt 6.10.3 / Ninja Multi-Config）
├── docs/                   本目录：架构、规范、流程
├── scripts/verify/         开发期手工验证脚本（powershell）
├── sdk/                    契约层：纯头文件 INTERFACE 库，无二进制
│   └── ToolBoxPlugin.h     唯一契约：ToolMeta / ToolSettings / IToolPage / IToolPlugin
├── app/                    外壳层：主程序
│   ├── main.cpp            应用元信息（组织名/应用名/版本）与入口
│   ├── core/               外壳的纯逻辑（无 QWidget 依赖），编成 ToolBoxCore
│   ├── MainWindow.*        窗口装配、导航渲染、配置持久化
│   └── ToolRegistry.*      插件扫描与装载
├── plugins/<工具名>/        工具层：一个工具 = 一个 MODULE 库（DLL）
│   └── core/               该工具的纯逻辑（无 QWidget 依赖），编成 <工具名>_core
└── tests/                  Qt Test 用例，一个测试一个目标，由 CTest 驱动
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

## 4. 插件契约

契约定义在 [ToolBoxPlugin.h](../sdk/ToolBoxPlugin.h)，是**唯一**的跨模块接口。

### 4.1 工具标识

- `ToolMeta::id` 必须匹配 `^[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*)+$`，形如 `分类.工具名`。
- 现有取值：`text.base64`、`dev.json-format`、`media.video-download`。
- **id 一旦随版本发布，永久不可更改**——它同时是配置键前缀、收藏/最近使用的持久化
  依据（`ui/favorites`、`ui/recent`）。改名等于让所有用户丢配置。
- 页面里所有持久化键都以 id 做前缀隔离，插件之间不会串键。

### 4.2 版本与 ABI

- 插件接口 IID 已版本化：`com.toolbox.ToolBox/IToolPlugin/1.0`。
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
- 推论：需要随外部状态改变返回值（例如按当前语言改写 `name`）**不允许**靠重新调用
  `meta()` 实现，必须换 id 走一次完整的重新装载。
- 缓存的原因是 `meta()` 是跨 DLL 的虚调用，而排序比较器会被调用 O(n log n) 次。

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
  现有实例：抖音渲染启动的 `waitForStarted(5000)`、析构时的 `waitForFinished(2000)`。

## 6. 状态与持久化

- 组织名/应用名固定为 `ToolBox`（在 `main.cpp` 中于任何 `QSettings` 构造之前设置），
  存储后端为注册表。
- 键的命名空间划分：
  - `ui/*` —— 外壳专用（`ui/lastToolId`、`ui/favorites`、`ui/recent`），插件不得读写；
  - `plugin/<工具 id>/*` —— 该工具专属，由 `ToolSettings` 自动加前缀，插件只写裸键名。
- 插件**不允许**直接构造 `QSettings`，一律通过 `toolbox::ToolSettings`。
- 持久化数据必须能在工具消失后自愈：外壳在每次装载后剔除指向已不存在工具的 id。
- 缓存/临时文件只放系统临时目录，且必须在任务结束与页面析构时清理；
  含登录凭据的文件（如 cookies 副本）不得长期驻留。

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
- 运行时可执行文件的查找顺序固定为：**手动指定 → `<exe 目录>/tools/bin/` → PATH**。
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
| 9.2 | 单个页面类曾承担界面、进程、网络、解压、解析、平台适配 | [videodl/VideoDlPlugin.cpp](../plugins/videodl/VideoDlPlugin.cpp) | 抖音适配与内核下载为后期追加 | **已收敛（P1）**：解析规则抽到 `plugins/videodl/core/` 并编成 `videodl_core`，页面只留界面、进程与网络 | 剩余的 `DownloadService`、`DouyinResolver` 拆分暂缓，理由见 §10 D2 |
| 9.3 | ~~无自动化测试，纯逻辑靠手工脚本验证~~ | 全项目 | 一直以手工验证推进 | **已消除（P1）**：`tests/` 下 5 个 Qt Test 目标，`ctest` 全绿 | 新增 `*/core/` 模块必须同步补用例（workflow §5） |
| 9.4 | ~~验证脚本混在可再生成的 `build/` 目录内~~ | `build/*.ps1` | 顺手放置 | **已消除（P0）**：脚本迁至 `scripts/verify/`，固定样本在 `scripts/verify/fixtures/` | 脚本运行时的截图/下载产物仍落在 `build/` —— 那些是可再生成物，属于正确位置 |
| 9.5 | ~~版本号硬编码两处~~ | [CMakeLists.txt](../CMakeLists.txt)、[main.cpp](../app/main.cpp) | — | **已消除（P2）**：版本号只留顶层 `project(... VERSION ...)`，由 `app/CMakeLists.txt` 的 `TOOLBOX_VERSION` 编译定义传给 `main.cpp` | 无 |
| 9.6 | ~~部署使用 `--no-translations`，Qt 自带对话框按钮为英文~~ | [app/CMakeLists.txt](../app/CMakeLists.txt)、[main.cpp](../app/main.cpp) | 早期为避免拷贝多余文件 | **已消除（P2）**：deploy 改为 `--translations zh_CN`，并在 `main.cpp` 里安装 `QTranslator`（只拷文件不装翻译器无效） | 无 |
| 9.7 | 插件 `CMakeLists.txt` 样板重复 | `plugins/*/CMakeLists.txt` | 复制目录即建新插件的模板 | 接受 | 出现第 5 个插件时抽取 `toolbox_add_plugin()` |
| 9.8 | ~~排序比较器中反复调用 `plugin->meta()`~~ | [ToolRegistry.cpp](../app/ToolRegistry.cpp) | — | **已消除（P2）**：装载时取一次存进 `Entry::meta`，比较器只读缓存；`meta()` 的无副作用契约见 §4.3 | 无 |
| ~~9.9~~ | ~~静态检查未接入 CI~~ | 根目录 `.clang-format` / `.clang-tidy`、`.git-blame-ignore-revs`、`scripts/verify/verify_format.ps1`、`.github/workflows/ci.yml` | 格式曾无法强制：归一化前 `.clang-format` 会让 15 个文件 / 328 行发生改动（clang-format 没有「保留手工换行」的选项，且只有一个全局 `AfterEnum` 开关） | **已消除（P3）**：格式在 73b1e4a 一次性归一化（字符多重集比对确认只动空格与换行），此后由 `verify_format.ps1` 强制、提交登记进 `.git-blame-ignore-revs`；CI 已接入 GitHub，push / PR 时在干净机器上跑四个快检 + 双配置构建 + `ctest` + 打包。命名检查（`verify_naming.ps1`）刻意**不进 CI**：clang-tidy 钉不到同一版本（PyPI 无 22.1.3，精确版本只在 LLVM 821 MB 归档里），**钉不住版本的工具不当门禁**，见 [workflow.md §3.2](./workflow.md#32-命名检查clang-tidy手动跑) | 无。CI 覆盖不到的界面与下载链路、以及命名检查，仍属手工回归（[workflow.md §11](./workflow.md#11-持续集成ci)） |
| 9.10 | ~~无 `install()` / CPack 打包规则，交付靠手工拷贝 `bin/`~~ | 顶层 [CMakeLists.txt](../CMakeLists.txt) | 交付频次低 | **已消除（P3）**：顶层整目录 `install()` + CPack 出 zip，版本号仍只有 `project(... VERSION ...)` 一个来源；两条 `install(CODE)` 保护会拦下「打错配置」与「忘了 deploy」，实测都会报错停下 | 发布流程见 [workflow.md §9](./workflow.md#9-发布)；`verify_shell.ps1 -Exe` 用于验收解压后的产物 |

## 10. 目标架构与迁移计划

**不推倒重来。** 迁移与功能改动绑定：只有当某个文件因新需求被改动时，才顺带完成它
对应的拆分。这样每一步都有验收标准，且不需要为重构单独排期。

### D1 · 外壳拆分（触发条件：下次改动 `MainWindow` 相关文件）

```
app/
├── main.cpp
├── core/                       无 QWidget 依赖，可单测
│   ├── ToolRegistry.*          现有，保持不变
│   ├── ToolCatalog.*           新增：工具条目模型 + 搜索过滤 + 收藏/最近使用
│   └── IconFactory.*           新增：无图标时的占位图标生成
└── ui/
    ├── MainWindow.*            仅装配与转发
    └── NavPanel.*              新增：导航列表的构建与分区渲染
```

验收标准：
1. `core/` 下的文件不 include 任何 `QtWidgets` 头文件。**这一条不需要 grep 兜底**：
   `ToolBoxCore` / `videodl_core` 都是静态库且只链接 `Qt6::Core`，包含界面类头文件会
   直接编译失败；反向依赖（`plugins` ↔ `app` 互相 include）另由
   `scripts/verify/verify_conventions.ps1` 检查；
2. 搜索过滤、收藏/最近使用的增删与去重有 Qt Test 用例；
3. 手工验证脚本 `scripts/verify/verify_recent.ps1` 全通过。

**进度（P1：核心部分已完成）**：[ToolCatalog.*](../app/core/ToolCatalog.h) 已落地，
`MainWindow` 只保留控件装配与渲染；`tst_toolcatalog` 覆盖过滤器与列表维护，
`verify_recent.ps1` 实测通过。验收标准 1–3 均已满足。

**决定：`ui/MainWindow`、`ui/NavPanel`、`IconFactory` 这三项不再做。** 它们是纯表现层
搬运：`MainWindow` 剩下的代码就是「建控件、按模型刷列表、转发信号」，再切出一个面板类
只会多一层无收益的转发；占位图标生成虽然无 QWidget 依赖，但它只被窗口调用一处，
抽出去换不来可测性。触发条件改为「出现第二个需要渲染导航分区的窗口时」。

### D2 · 视频下载插件拆分（触发条件：下次扩展该插件）

```
plugins/videodl/
├── VideoDlPlugin.*             插件入口与 meta()（现状：VideoDlPage 也在这里）
├── core/                       无 QWidget 依赖，可单测 —— P1 已完成
│   ├── OutputParsing.*         输出解码、剥色、地址提取、进度/阶段/产物解析
│   ├── DouyinSupport.*         站点判定、画质档位、DOM 字段提取
│   ├── EngineLocator.*         内核定位顺序（手动 → 随程序目录 → PATH）
│   └── CookieFile.*            cookie 规范化
├── DownloadService.*           待做：进程 / 网络 / 断点续传
└── DouyinResolver.*            待做：浏览器渲染取流
```

`core/` 编成静态库 `videodl_core`（PUBLIC 暴露头文件目录），插件本体链接它；
页面侧只保留两件只有它才能做的事：把 `QCoreApplication` 接上（`engineDir` 适配器）
和 `tr()` 文案。

验收标准：
1. `VideoDlPage` 不再直接持有 `QProcess` / `QNetworkAccessManager`；
2. cookie 规范化、地址提取、进度解析、`video_id` 解析均有 Qt Test 用例；
3. 抖音与 B站 各完成一次真实下载回归。

**进度（P1：解析部分已完成）**：四个 `core/` 模块已抽出并接入测试，验收标准 2 满足。

**剩余部分暂缓**：`DownloadService` 与 `DouyinResolver` 目前各只有一处调用点，
且都强依赖 `QProcess` / `QNetworkAccessManager` 的生命周期；拆出去会先引入一层只有
自己用的间接，收益要等第二次扩展才出现。触发条件保持「下次扩展该插件时」，
届时连同验收标准 1、3 一起补齐。

## 相关文档

- [coding-standards.md](./coding-standards.md) —— 命名、内存、信号槽等编码规则
- [workflow.md](./workflow.md) —— 构建、测试、加插件、发布与文档变更流程
