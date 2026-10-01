# 工具箱 · 编码规范

> 状态：生效中 · 最后修订：2026-10-01
>
> 本文件规定「怎么写代码」。架构分层与职责边界见 [architecture.md](./architecture.md)，
> 流程类约定见 [workflow.md](./workflow.md)。
>
> 规则分两级：**硬规则**是评审必须拦下的，**建议项**允许按场景判断。
> 能用工具保证的规则不依赖人自觉（见 §12 检查手段映射表）。
> 有意的例外必须登记到 [architecture.md §9 偏差台账](./architecture.md#9-偏差台账)。

## 1. 命名

| 对象 | 规则 | 示例 |
| --- | --- | --- |
| 类 / 结构体 / 枚举 / 枚举值 | 大驼峰 | `MainWindow` `ToolMeta` `FetchKind::YtDlp` |
| 抽象接口 | `I` + 大驼峰 | `IToolPlugin` `IToolPage` |
| 成员变量 | `m_` + 小驼峰 | `m_registry` `m_fetchPartPath` |
| 文件级 / 静态常量 | `k` + 大驼峰 | `kNavPanelWidth` `kMaxFetchAttempts` |
| 函数 / 方法 | 小驼峰，动词开头 | `rescan()` `normalizeCookies()` `refreshEngineStatus()` |
| 槽函数 | `on` + 事件名 | `onNavRowChanged()` `onProcessFinished()` |
| 信号 | 名词短语或过去式 | `textChanged` `finished` `customContextMenuRequested` |
| 局部变量 / 参数 | 小驼峰 | `rawInput` `droppedRows` `audioOnly` |
| 布尔 | 语义词：`is` / `has` / `needs` / 明确形容词 | `isDouyinUrl()` `hasMatch` `needsReferer` |
| 缩写在**类型名**中 | 全大写 | `URL` `ID` `JSON` `QJsonParseError` |
| 缩写在**变量名**中 | 全小写 | `urlRe` `ytDlp` `videoId` |
| 自定义宏 | 全大写 + 下划线，带项目前缀 | `ToolBoxPlugin_iid` |
| 文件名 | 与其中主类名一致，大驼峰 | `MainWindow.cpp` `VideoDlPlugin.h` |
| 插件目录名 | 全小写，无分隔或连字符 | `plugins/base64/` `plugins/videodl/` |

硬规则：
- 成员变量必须有 `m_` 前缀；所有用户可见的界面文案由 §7 约束。
- 一个名字在项目内只表达一个含义；缩写一律按上表统一，不允许 `ytDlp` / `ytDLP`
  这类大小写漂移。
- 命名要「见名知意」：不出现 `tmp`、`data2`、`doIt()`、`handle()` 这类名字。
  临时变量只在函数体极短（≤5 行）时才允许 `tmp`。

## 2. 文件组织

- 一个类一个文件，`.h` 声明 + `.cpp` 实现，文件名与类名一致。
- 插件页面类若仅供本插件使用，允许直接定义在插件 `.cpp` 内；带 `Q_OBJECT` 时
  必须在文件末尾 `#include "Xxx.moc"`。**硬规则**：这类页面不得被其它文件使用，
  一旦第二个文件需要它，就必须拆成独立文件。
- 头文件只放声明：优先使用前向声明而非 include；不 include 用不到的头文件。
- 头文件必须有 `#pragma once`，且不依赖「谁先 include 了谁」。
- 单个文件超过约 600 行，或者单个类超过约 10 个职责明确的成员函数时，
  必须评估拆分，并在评审中给出留下或拆分的结论。

## 3. 内存与所有权

- **优先使用 Qt 父子对象树**：`new` 出来的 `QObject` 只要活了，就必须指定 parent
  或明确移交给一个持有者。
- 跨对象树持有的对象用 `QScopedPointer`（独占）或 `QPointer`（可失效引用）。
  裸指针只允许表达「不拥有」。
- `QNetworkReply` 一律用 `deleteLater()` 释放；不得在 `finished` 处理中 `delete`。
- 插件相关：`QPluginLoader` 由 `ToolRegistry` 持有，卸载顺序见
  [architecture.md §4.4](./architecture.md#44-生命周期约束)。
- 硬规则：不允许出现「`new` 之后既不设 parent、也不显式释放、也没有智能指针接管」
  的对象；不允许返回指向容器内部元素的指针并让调用方长期持有
  （容器扩容/清空即悬空）。

## 4. 信号槽

- **只用新式语法**（函数指针或 lambda）。禁止 `SIGNAL()/SLOT()` 宏与
  `QMetaObject::invokeMethod` 的字符串重载——它们把类型错误推迟到运行时。
- lambda 连接**必须**传 context 对象（`this` 或目标控件），保证对象销毁后连接自动断开。
- 连接的生命周期要能一眼看出：优先在构造或 `buildUi()` 中集中连接，不要在事件处理里
  反复 `connect` 造成重复触发。
- 重入要显式防护：信号处理中触发会再次发出同一信号的操作时，必须用可读的守卫
  （如 `m_applyingFilter` 标志）或推到事件循环下一轮（`QTimer::singleShot(0, ...)`）。

## 5. 常量与魔法数字

- 所有有含义的字面量必须提为 `constexpr` 或 `const` 常量，并给中文注释说明其含义。
- 重复出现两次以上的字符串常量同样要提取，避免改一处漏一处。
- 数值必须有单位或用命名表达单位：`kMaxFetchAttempts`（次）、
  `setTransferTimeout(30000)`（ms，需注释）。

## 6. 错误处理

- 只在系统边界校验：用户输入、外部程序输出、网络响应、文件内容。
  内部调用点之间信任契约，不做冗余校验。
- 不用异常做控制流（Qt 与 MSVC 环境下异常开销与可读性都不划算）。
- 失败路径必须给出可操作的信息：现象 + 原因 + 下一步怎么做。
- 外部数据的防御性解析集中在边界函数内（如 cookie 规范化、DOM/输出解析），
  内部只用已校验的数据。

## 7. 字符串、国际化与路径

- 面向用户的字符串**必须**用 `tr()` 包裹（项目已全量做到，继续保持）。
  不面向用户的字面量用 `QStringLiteral()`，不用 `QString("...")` 的隐式转换。
- 拼接路径统一用 `QLatin1Char('/')`；只在**显示给用户**时用
  `QDir::toNativeSeparators()`。
- 用户可见的区域分隔符用 `QLatin1Char` 常量，不硬编码中文标点之外的符号。
- `tr()` 里不要拼可变数据；先 `tr("...%1...").arg(...)`，保证可翻译。

## 8. 注释与文档

- 注释**一律用中文**，解释「为什么这么做」，不重复代码已经说清的「做了什么」。
- 公开接口、契约类、非直觉实现的**必须**写 Doxygen 风格注释（`///`），
  至少包含：用途、参数/返回值约定、调用时机。
- 涉及外部系统怪癖的地方（第三方接口、编码、压缩包结构、平台差异）必须写明
  现象与依据，否则以后没人敢改。
- 注释与代码不一致时，以代码为准并立即修正注释——过时的注释比没有注释更糟。

## 9. C++ 与 Qt 语言约束

- C++ 标准为 17（`qt_standard_project_setup()` 的默认值）。不允许使用更高标准的特性。
- 只用 Qt 6 API，不引入 Qt 5 兼容分支；不使用已标记弃用的 API。后者不需要评审盯着：
  触碰弃用 API 会产生 `C4996`，在 `/WX` 下直接变成编译错误（实测确认）。
- 新增 Qt 模块依赖时，必须同时在顶层 `CMakeLists.txt` 的 `find_package` 中声明
  （例如 `Network`）。
- MSVC 下必须保留 `/utf-8`（源码含中文字面量），不允许以「本地代码页能用」为由绕过。
- MSVC 下必须保留 `/W4 /permissive- /WX`：**警告即错误**，「先合进去、以后再清告警」
  在这套配置下不可能发生。新增代码带出告警时，要么改掉，要么就地写明为什么必须
  抑制（`#pragma warning(push/pop)` + 注释），不允许整体降警告等级。
- 源文件统一 UTF-8 无 BOM、换行符统一 LF。由 `.editorconfig` 与 `.gitattributes`
  共同保证，不依赖个人编辑器设置。**唯一例外是 `*.ps1`：必须带 UTF-8 BOM** ——
  Windows PowerShell 5.1 按系统 ANSI 代码页解码无 BOM 的脚本，会把 UTF-8 的中文注释
  解错到语法层面（本项目因此遇到过一次凭空多出的 `}` 导致脚本无法解析）。原因与检查
  见 [workflow.md §3.1](./workflow.md#31-代码风格检查)。

## 10. CMake 约定

- 使用现代 target-based 写法（`target_link_libraries` / `target_include_directories`），
  不使用全局变量式的 `include_directories` / `link_libraries`。
- 顶层集中决定输出布局（`TOOLBOX_BIN_DIR`），子目录只引用，不自行推导路径。
- 插件统一输出到 `TOOLBOX_BIN_DIR/tools`，不指定固定文件名（外壳按目录扫描）。
- **`install()` 规则只写在顶层**，而且只装整个 `bin/<Config>/`，不在子目录里逐个
  `install(TARGETS)`。原因有二：一是这个目录本身就是交付形态，Qt 运行时、翻译、
  外部内核都不是本工程的目标却必须一起交付；二是新增插件因此不需要改顶层文件，
  与架构 §1「新增工具不改外壳」保持一致。打包流程见 [workflow.md §9](./workflow.md#9-发布)。
- 插件的图片/图标资源用 `qt_add_resources` 编进 DLL，不做外部文件依赖。
- 新增插件时必须在顶层 `CMakeLists.txt` 显式 `add_subdirectory`（保持显式，不用 glob）。

## 11. 界面代码约定

- 控件命名：成员 `m_` + 语义名（`m_download`、`m_progress`），局部控件按用途命名
  （`encodeButton`、`qualityRow`）。
- 布局对象在函数内用完即弃，不留无用的成员指针。
- 按钮的可用状态集中在一处更新（如 `updateBusyState()`），不在各回调里零散地
  `setEnabled`。
- 长文本控件设置 `setWordWrap(true)` 与可选中；日志类控件必须限制最大块数，
  防止内存无限增长。
- 状态提示必须覆盖「正在做什么 + 进度 + 失败原因」三要素（见
  [architecture.md §7](./architecture.md#7-错误处理与用户反馈)）。

## 12. 检查手段映射表

| 规则 | 靠什么保证 | 现状 |
| --- | --- | --- |
| 缩进、行宽、大括号位置、符号对齐、include 排序 | `.clang-format` + `scripts/verify/verify_format.ps1`（`clang-format --dry-run --Werror`） | **已达标**：全仓已于 73b1e4a 归一化（15 文件 / 328 行，字符多重集比对确认只动空格与换行），此后由脚本强制，见 workflow §3.1 |
| 字符集、缩进风格、末尾空行、行尾空白 | `.editorconfig` + `scripts/verify/verify_whitespace.ps1` | 已配置且有脚本检查（可纳入 CI） |
| `*.ps1` 带 UTF-8 BOM | `verify_whitespace.ps1` 查每个脚本文件的前三个字节 | 已达标（13 个脚本全部带 BOM；已做去掉 BOM 的反向验证） |
| 换行符统一 LF | `.gitattributes`（`* text=auto eol=lf`） | 已配置（本机 `core.autocrlf=true`，必须靠它兜底） |
| 层间依赖方向（`plugins` / `app` / `sdk`） | `scripts/verify/verify_conventions.ps1` 查跨层 include；`core/` 静态库只链接 `Qt6::Core`，误用界面类会直接编译失败 | 已达标 |
| 显式 `tr()` / `QStringLiteral()` | `verify_conventions.ps1` 查 `QString("字面量")` 这类构造；`tr()` 覆盖的是否完备靠评审 | 已达标 |
| 新式信号槽语法 | `verify_conventions.ps1` 排除 `SIGNAL(` / `SLOT(` | 已达标 |
| QSettings 键命名空间（`ui/*` 与 `plugin/<id>/*`） | `verify_conventions.ps1` 查裸字符串键；正常路径由 `ToolSettings` 封装 | 已达标 |
| 头文件 `#pragma once` | `verify_conventions.ps1` 查每个 `.h` 的前三行 | 已达标 |
| `#include "Xxx.moc"` 必须在文件末尾 | `verify_conventions.ps1` 查该 include 之后是否还有代码 | 已达标 |
| 父子对象树与所有权 | 评审。clang-tidy 的 `cppcoreguidelines-owning-memory` 已评估并**排除**（与 Qt 父子对象树惯用法冲突，全仓 61 条全是误报），见 workflow §3.2 | 已达标 |
| `m_` / `k` 前缀、命名一致 | `scripts/verify/verify_naming.ps1`（clang-tidy `readability-identifier-naming`，配置见根目录 `.clang-tidy`） | 已达标：全仓 0 命中，且已做注入式反向验证。**手动跑**：工具版本钉不住，不进 CI，理由见 workflow §3.2 |
| 纯逻辑可单测 | 评审（对照 §architecture 3 的判定特征）+ `ctest` | 已达标：`app/core`、`plugins/videodl/core` 均有 Qt Test 用例，见 workflow §5 |
| 编译警告不引入新告警 | MSVC `/W4 /permissive- /WX`（警告即错误） | 已达标：Debug 与 Release 全量重建 0 告警 |
| 不使用已标记弃用的 API | 同一个 `/WX`：弃用告警是 `C4996`，在 `/WX` 下直接编译失败（实测确认） | 已达标 |
| C++ 标准不超标 | 编译器约束（`CMAKE_CXX_STANDARD`） | 已达标 |

保证手段按可靠性排序：**编译器 > 脚本 > 评审**。同一条规则如果评估后能用更靠前的
手段保证，就应该下移 —— 本轮把「不引入新告警」「空白与编码」「跨层 include / 旧式
信号槽 / 字符串字面量 / QSettings 键」「`#pragma once` / moc 位置」分别下移到了
编译器和 `scripts/verify/` 的四个脚本，「命名前后缀」下移到了 clang-tidy，
「缩进 / 行宽 / 大括号 / include 排序」下移到了 clang-format（先做一次性归一化，
再用脚本锁住）。**四个脚本已在 CI 里跑**（push / PR 时，见
[workflow.md §11](./workflow.md#11-持续集成ci)），所以「本地忘了跑」不再等于「没人跑」；
命名检查与 clang-tidy 那一步仍要手动跑（workflow §3.2），因为它的工具版本钉不住 ——
**钉不住版本的工具不当门禁**，否则检查结果会随环境摇摆。剩下仍靠评审的两条
（`tr()` 完备性、对象所有权）都需要语义分析：前者要判断字符串是否真的面向用户，
后者要判断裸指针的持有者是谁，两者都不是「看名字」能定的。

## 相关文档

- [architecture.md](./architecture.md) —— 分层、职责边界、插件契约、偏差台账
- [workflow.md](./workflow.md) —— 构建、测试、加插件、发布与文档变更流程
