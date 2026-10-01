# 工具箱 · 工程流程规范

> 状态：生效中 · 最后修订：2026-10-01
>
> 本文件规定「怎么做事」：环境、构建、加插件、测试、版本、发布，以及
> **文档与代码如何保持一致**。架构见 [architecture.md](./architecture.md)，
> 编码规则见 [coding-standards.md](./coding-standards.md)。

## 1. 环境与工具链

| 项目 | 约定 | 依据 |
| --- | --- | --- |
| 编译器 | MSVC x64（VS2022 及以上；本机 VS18/v144） | 与 Qt 的 `msvc2022_64` ABI 兼容 |
| Qt | 6.12.0 `msvc2022_64`，路径 `C:/Qt6.12/6.12.0/msvc2022_64` | `CMakePresets.json` |
| CMake | ≥ 3.25 | 顶层 `CMakeLists.txt`（Qt 6.12 要求） |
| 生成器 | Ninja Multi-Config，构建目录固定 `build/` | `CMakePresets.json` |
| 打包 | CPack（随 CMake 提供，不必单独安装），产物为 zip | 顶层 `CMakeLists.txt`；流程见 §9 |
| 源码编码 | UTF-8，换行 LF；`*.ps1` 必须带 BOM，其余文件不带 BOM（原因见 §3.1） | 源码含中文，必须 `/utf-8` |
| 警告等级 | `/W4 /permissive- /WX`，**警告即错误**，Debug / Release 均须 0 告警 | 顶层 `CMakeLists.txt`；规则见 coding-standards §9 |
| 格式化 | clang-format 22.1.3，随 Visual Studio 提供，**不必单独安装**；配置见根目录 `.clang-format` | **已强制**：全仓已归一化，`verify_format.ps1` 把关，见 §3.1 |
| 静态检查 | clang-tidy（**LLVM 22.1 线**：本机用 VS 自带的 22.1.3，CI 装 PyPI 的 22.1.8）；配置见根目录 `.clang-tidy` | **已强制**：`verify_naming.ps1` 把关，已进 CI（排在构建之后），见 §3.2 |

Qt 安装相关的已知坑（历史踩过的，不要再试）：
- **不要**用清华镜像、华为云 `repo.huaweicloud.com/qt`、阿里云镜像安装 Qt：
  前者包已损坏会报 `ArchiveChecksumError`，后者无法正确定位 XML 文件。
- 用 aqt 安装时保持官方源默认并发设置，改并发可能导致安装程序空转。

## 2. 源码树里什么进版本控制

进版本控制：`CMakeLists.txt`、`CMakePresets.json`、`README.md`、`docs/`、`scripts/`、
`sdk/`、`app/`、`plugins/`、`tests/`、`.github/`（CI 配置）、`translations/`
（`.ts` 译文，见 §3.3），以及五份工具配置 `.clang-format`、`.clang-tidy`、
`.editorconfig`、`.gitattributes`、`.git-blame-ignore-revs`。

**不进版本控制**（已在 `.gitignore` 中声明）：`build/`（完全可再生成）、
IDE 目录、CMake 缓存。

推论：**任何不可再生成的东西都不许放在 `build/` 里**。构建产物、手工验证脚本、
测试数据、截图，都不属于 `build/`。

## 3. 日常构建

```powershell
# 配置（首次或改了 CMake 之后）
cmake --preset default

# 构建
cmake --build --preset debug
cmake --build --preset release

# 运行
.\build\bin\Debug\ToolBox.exe

# 跑单元测试（见 §5）
ctest --test-dir build -C Debug --output-on-failure

# 部署 Qt 运行时（把 Qt 的 DLL 收进输出目录，便于整目录分发）
cmake --build build --config Release --target deploy
```

输出布局：`build/bin/<Config>/ToolBox.exe` + `build/bin/<Config>/tools/*.dll`。

### 3.1 代码风格检查

风格规则分两层，**两层现在都可强制**：空白与编码一层，clang-format 一层。

**空白与编码这一层**：`.editorconfig` 里与编辑器无关的那几条 —— UTF-8、LF、文件末尾
恰好一个换行、不留行尾空白、源码不用制表符，外加 **`*.ps1` 必须带 UTF-8 BOM**。
检查手段是

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_whitespace.ps1
```

它扫全部纳入版本控制的文本文件，有违规就打印文件名与规则并返回非零退出码。

`*.ps1` 为什么要 BOM 这个例外：**Windows PowerShell 5.1 加载无 BOM 的脚本时按系统
ANSI 代码页解码**（本机是 936 / GB2312）。脚本里的中文注释是 UTF-8，被按 GB2312 解回来
就可能凑出一个多余的 ASCII 字符 —— 本项目实际踩到过：一段中文注释凭空多出一个 `}`，
脚本直接无法解析（`Unexpected token '}'`），而用 UTF-8 解码同一个文件却有 0 个错误。
带 BOM 时加载器改按 UTF-8 读，中文注释才安全。这个坑不看字节根本发现不了，所以交给
脚本强制，而不是靠人记住。`.editorconfig` 的 `[*.ps1]` 段与
`scripts/verify/verify_whitespace.ps1` 头部都有这条说明。

**clang-format 这一层**：`.clang-format` 已于 73b1e4a 完成一次性全仓归一化，
并改由 `scripts/verify/verify_format.ps1` 强制：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_format.ps1
```

它用 `clang-format --dry-run --Werror` 扫全部 C++ 源文件，只读不写，有不符合就
列出文件并给出修复命令。clang-format 来自 Visual Studio，路径
`...\VC\Tools\Llvm\x64\bin\clang-format.exe`，**不必单独安装**；本机版本 22.1.3。

归一化这件事是怎么做的、代价是什么：

1. **一次性改动 15 个文件 / 328 行**（146 增 182 删，净减是因为手工折断的长调用
   被合回一行）。
2. **怎么确认它没动语义**：不是靠看 diff，而是逐文件比对前后两版的**字符多重集**
   —— 统计结果里唯一有增减的字符是空格与换行，其余字符（标识符、字符串字面量、
   标点、注释里的中文）计数完全一致。这条比对照重排和换行都不敏感，恰好能穿透
   `SortIncludes` 的重排。再加 Debug / Release 全量重建 0 告警、`ctest` 5/5、
   外壳冒烟实测通过。
3. **变更性质**：clang-format 没有「保留手工换行」的选项，手工折断的长调用会被合
   回一行；空函数体 `{` `}` 收成 `{}`；include 按 `CaseSensitive` 在分组内重排
   （`IncludeBlocks: Preserve` 保住了 `#include "Xxx.moc"` 必须在文件末尾这条硬约束，
   `verify_conventions.ps1` 也在盯着）。中文注释一个字符没动（`ReflowComments: false`）。
4. **为什么必须单独成一个提交**：否则 `git blame` 会把整段历史都归到这一次重排上。
   归一化提交的 hash 登记在 `.git-blame-ignore-revs`，用之前先配一次：

   ```powershell
   git config blame.ignoreRevsFile .git-blame-ignore-revs
   ```

   **今后任何纯格式提交都要这样登记**；一旦某个提交同时改了语义，登记它就会掩盖
   真实改动，所以这条规则只适用于「只动格式」的提交。

### 3.2 命名检查（clang-tidy）

[clang-format 管不了命名](#31-代码风格检查)，命名规则（[coding-standards.md §1](./coding-standards.md#1-命名)）
靠根目录 `.clang-tidy` + clang-tidy 的 `readability-identifier-naming` 检查。clang-tidy
随 Visual Studio 提供（`...\VC\Tools\Llvm\x64\bin\clang-tidy.exe`），本机不必单独安装。

`CMakePresets.json` 已打开 `CMAKE_EXPORT_COMPILE_COMMANDS`，所以**不需要额外配置任何
目录**，平时那个构建目录里就有 `compile_commands.json`。**先构建 Debug**，然后跑：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_naming.ps1
```

**先构建**这条不能省：`#include "Xxx.moc"` 指向构建期由 AUTOMOC 生成的文件，没构建过
它们就不存在，缺这些文件的 12 个源文件（2 个插件入口 + 10 个测试）会被跳过分析，结果
看着「全绿」其实是没查。`compile_commands.json` 里每个文件有 Debug/Release 两条，
clang-tidy 取第一条（Debug），所以构建 Debug 即够。

`.clang-tidy` 只开白名单，刻意**不含** `cppcoreguidelines-owning-memory`：它要求 `new`
出来的裸指针必须赋给 `gsl::owner<>`，与本项目（及 Qt 整体惯例）用父子对象树表达所有权
的写法正面冲突，全仓实跑 61 条命中**全是误报**。所有权一条继续靠评审。

脚本除了方便，还堵一个**会假通过**的坑：`Xxx.moc` 缺失时 clang-tidy 会直接跳过
那几个翻译单元，输出看上去全绿、其实什么都没查。所以脚本把
`clang-diagnostic-error` 当成硬失败并打印修复命令。这条做过反向验证：删掉构建目录里
的 `.moc` 再跑，上面那 12 个源文件被逐一点出来并返回 1。

**结论（P3）**：命名规则**当前全仓 0 命中** —— 23 个 `.cpp` 加上 `HeaderFilterRegex`
覆盖的头文件，没有任何一处违反 §1。这条结论也做过反向验证：手工把 `ToolCatalog.cpp`
里的一个局部变量改成 `HasVisibleChild_XX`，检查立刻报出
`invalid case style for local variable`。

**版本策略：钉发行线，不钉精确版本**（与 `verify_format.ps1` 的精确钉不同，别照着改）：

- 格式化器钉**精确版本** 22.1.3，因为输出逐字节敏感 —— 另一个补丁版可能把代码排成
  别的样子。这个精确版本恰好等于 Visual Studio 自带的那份，所以本机不用额外装东西。
- clang-tidy 没有这种「本机现成」的精确版本：VS 18 自带 22.1.3，而 PyPI 上只有
  22.1.0 / 22.1.0.1 / 22.1.7 / 22.1.8，精确的 22.1.3 只能从 LLVM 的
  `clang+llvm-22.1.3-x86_64-pc-windows-msvc` 归档（**821 MB**）里拿。若钉精确版本，
  每个开发者都要为跑一条检查单独装一份。
- 所以钉的是 **LLVM 22.1 这条线**：命名判定不会在同一条线的补丁版之间变，而 VS 升级
  到新线（22.2、23.x）会**当场 FAIL**，不会悄悄换一套判定。

版本不符合预期时脚本直接 FAIL、绝不给绿 —— 这就是「检查结果不随环境摇摆」的可执行
定义。CI 里照此跑（装 PyPI 的 `clang-tidy==22.1.8`），排在构建之后，见 §11。

### 3.3 界面语言与翻译

**源语言是中文**：`tr()` 里写的就是中文，中文界面不需要任何翻译文件。英文译文在
`translations/toolbox_en.ts`，由顶层 `CMakeLists.txt` 的 `qt_add_translations` 用
lrelease 编成 `.qm`，再**编进可执行文件的资源**（`RESOURCE_PREFIX "/i18n"`）——
不落成外部文件，就不存在「翻译文件忘了一起拷」这种失败，直接跑构建产物也有译文。

**语言在启动时定一次，运行期不切换**（改语言要重启）。取值来自配置 `ui/language`：
**空 = 跟随系统**，非空按语言代码强制（只认 `en*` / `zh*`，认不出来的值退回跟随系统）。
判定逻辑在 `app/core/LanguageChoice.*`，脱离界面可单测。命令行改语言：

```powershell
# 强制英文（改完要重启程序）
New-Item -Path "HKCU:\Software\ToolBox\ToolBox\ui" -Force | Out-Null
Set-ItemProperty -Path "HKCU:\Software\ToolBox\ToolBox\ui" -Name language -Value "en"
# 恢复跟随系统
Remove-ItemProperty -Path "HKCU:\Software\ToolBox\ToolBox\ui" -Name language
```

验证：`verify_shell.ps1` 有 `-Lang en|zh`，两组断言分别对应两种语言的界面
（含 Qt 自带对话框按钮的文案）：

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_shell.ps1 -Lang en
```

维护译文时要留意的三件事：

1. **骨架可以自动刷新，译文仍要人来定**。`lupdate` 现在跑得起来（Qt 6.12 这份安装里
   有 `Qt6Qml.dll`，实测 `lupdate version 6.12.0`），刷新骨架走：

   ```powershell
   powershell -ExecutionPolicy Bypass -File scripts\lupdate_ts.ps1
   ```

   它调 `update_translations` 目标（脚本自己拼 MSVC 环境并把 Qt 的 `bin` 放进 `PATH`，
   日志落在 `scripts/lupdate.log`）。但 `lupdate` 只同步 `<source>` 与位置，**新译文的
   英文要人来写** —— 所以规矩不变：**改了 `tr()` 里的字面量之后，必须让 `.ts` 里的
   `<source>` 同步**，否则运行期按「上下文 + 源字符串」查不到译文，会静默退回中文。
   顺带一提：`<location>` 里的行号只是给 Linguist 跳转用的提示，会随代码改动漂移，
   不影响译文生效，不必手工维护。
2. **同一个源字符串只能有一个译文**。「收藏」既做导航分区标题（英文要 Favorites）
   又做右键菜单动作（英文要 Add to favorites）时，必须**让源字符串分开**（现取
   「加入收藏」）—— Qt 的 `//: 消歧注释` 在运行期查不到，解决不了这个问题。
3. **用户可见的字符串一律 `tr()`**。这条有新加的机械检查兜着
   （`verify_conventions.ps1` 第 7 条：UI 目录下不允许出现含中文的
   `QStringLiteral`），因为在这之前 base64 插件的界面文案全是 `QStringLiteral`，
   而文档当时写着「已全量做到」。

## 4. 新增一个工具插件

按顺序做，每一步都有明确产出：

1. 复制 `plugins/base64/` 为新目录，目录名全小写（如 `plugins/regextool/`）；
2. 重命名 `Base64Plugin.*` 为新插件名，同步改类名与 `CMakeLists.txt` 里的目标名；
3. 在 `meta()` 中填写：`id`（遵守 [architecture.md §4.1](./architecture.md#41-工具标识)
   的格式与不可变约定）、`name`、`category`、`version`、`description`；
   **同步 `metadata.json.in` 里同样的四个字段**（name/category/description/version）。
   这份静态元数据是宿主 `load()` 之前唯一的依据，`abi` 那一行由构建注入、
   不要手改（[architecture.md §4.2](./architecture.md#42-版本与-abi)）；
4. 需要图标就放 `icons/*.svg` 并在 `CMakeLists.txt` 里加 `qt_add_resources`；
   不需要图标则留空，外壳会自动生成首字占位图标；
5. 在顶层 `CMakeLists.txt` 中 `add_subdirectory(plugins/<新目录>)`；
6. 页面需要持久化则实现 `IToolPage`，键名只用裸名，由 `ToolSettings` 加前缀；
7. 纯逻辑（解析、转换、过滤）必须放进独立的无 UI 依赖的类或自由函数，
   并补 Qt Test 用例（见 §5）；
8. 更新本文档 §8 的脚本清单（若新增了手工验证脚本）；
9. 把新插件的**目标名加进顶层 `CMakeLists.txt` 的 `qt_add_translations(... SOURCE_TARGETS)`
   列表**，否则它的界面文案不会被抽取成译文（§3.3）。

**不允许**为了让新工具跑起来而修改 `app/` 里的代码。如果需要改外壳，
说明契约不足，应先走 §7 的契约变更流程。

## 5. 测试规范

判断底线：**凡是放在 `*/core/` 下的代码，都必须有对应的 Qt Test 用例**。界面上点不
出来的逻辑，正是单元测试该覆盖的部分。

```powershell
cmake --build --preset debug
ctest --test-dir build -C Debug --output-on-failure
```

当前用例与覆盖对象：

| 测试 | 被测模块 |
| --- | --- |
| `tst_toolcatalog` | [app/core/ToolCatalog.*](../app/core/ToolCatalog.h)：导航过滤、失效 id 清理、最近使用去重与限长 |
| `tst_languagechoice` | [app/core/LanguageChoice.*](../app/core/LanguageChoice.h)：界面语言的解析（跟随系统 / 强制 / 配置写坏的兜底） |
| `tst_outputparsing` | [videodl/core/OutputParsing.*](../plugins/videodl/core/OutputParsing.h)：输出解码、剥色、地址提取、进度/阶段/产物解析、文件名消毒、`Content-Range` 起始偏移解析、命令行脱敏 |
| `tst_downloadargs` | [videodl/core/DownloadArgs.*](../plugins/videodl/core/DownloadArgs.h)：命名模板与百分号转义、仅音频、无 ffmpeg 的降级、四档画质、referer 与 cookies |
| `tst_douyinsupport` | [videodl/core/DouyinSupport.*](../plugins/videodl/core/DouyinSupport.h)：站点判定、画质档位、DOM 字段提取 |
| `tst_cookiefile` | [videodl/core/CookieFile.*](../plugins/videodl/core/CookieFile.h)：cookies 规范化与各失败分支 |
| `tst_enginelocator` | [videodl/core/EngineLocator.*](../plugins/videodl/core/EngineLocator.h)：内核定位顺序 |
| `tst_pluginmeta` | [sdk/ToolBoxPlugin.h](../sdk/ToolBoxPlugin.h)：静态元数据的 IID 门禁，以及 `toolMetaFromMetaData()` 的两层取值（`MetaData` → `toolbox`） |
| `tst_logger` | [app/core/Logger.*](../app/core/Logger.h)：重定向到文件、install 幂等、级别过滤、环境变量覆盖级别、轮转与备份份数 |
| `tst_pluginscanpolicy` | [app/core/PluginScanPolicy.*](../app/core/PluginScanPolicy.h)：装载门禁的每条分支（id 格式、abi 一致/缺失/宿主未注入） |
| `tst_jsonformat` | [jsonfmt/core/JsonFormat.*](../plugins/jsonfmt/core/JsonFormat.h)：JSON 解析与序列化、出错偏移与原因的回传 |
| `tst_mainwindow` | 外壳在**无插件**环境下的四条主路径：构造、扫描不存在目录、切页、搜索（不装载任何真实插件） |
| `tst_integration` | 跨 DLL 真链路：把 base64 / jsonfmt 部署到专属目录，验证 `rescan` → `qobject_cast` → `createPage` 全程可用 |

新增用例：在 `tests/` 下加一个 `tst_<模块名>.cpp`，用
`toolbox_add_test(tst_<模块名> <被测静态库>)` 注册，并同步本表。

约定：

- 默认用 `QTEST_APPLESS_MAIN` / `QTEST_GUILESS_MAIN`（无 `QApplication`），`*/core/` 的
  用例一律如此。**例外**：确实要装配真实控件的用例（`tst_mainwindow`、`tst_integration`）
  可以用 `QTEST_MAIN` 级的 `QApplication`，但必须在自己的 `main()` 里、**构造
  `QApplication` 之前**设 `QT_QPA_PLATFORM=offscreen`，且只验证「构造与装载不崩」。
  例外不能成为借口：**逻辑**若因为「挂了控件所以不好测」，第一选择仍是把逻辑挪进
  `core/` 再测，而不是加一个 GUI 用例糊过去。
- **不允许**依赖真实网络与真实子进程；需要文件系统时用 `QTemporaryDir`，不留残留。
- 测试可执行文件落在 `build/tests/<Config>/`，不混进要分发的 `bin/<Config>/`；
  测试进程的 `PATH` 由 `tests/CMakeLists.txt` 前置 Qt 的 `bin` 目录，
  因此没跑过 `deploy` 的干净构建也能直接启动。
- 界面与真实下载链路用 `scripts/verify/` 下的脚本验证（清单见 §8），
  它们属于**手工回归**，不替代单元测试。

## 6. 版本与提交

- **主程序版本号只有一个来源**：顶层 `CMakeLists.txt` 的 `project(... VERSION ...)`，
  通过编译定义传给 `main.cpp`，不在源码里重复硬编码（原偏差 9.5，P2 已消除）。
- 插件各自维护 `ToolMeta::version`，独立演进，与主程序版本无关。
- 提交信息用中文，写明「为什么改」而不是「改了哪个文件」；一个提交只做一件事。
- 遵循既有习惯：**功能积累后一次性发版，不逐次改版本号发版**。
- 破坏兼容性的改动必须在提交信息里显式声明（尤其涉及 §4.2 ABI 的部分）。

## 7. 契约变更流程（改动 `sdk/` 时）

这是本项目风险最高的操作，必须按顺序执行：

1. 先改 `docs/architecture.md §4`，说明改什么、为什么、对老插件的影响；
2. 按 [§4.2](./architecture.md#42-版本与-abi) 判断是否破坏 ABI；破坏则升 IID 版本号；
3. 改 `sdk/ToolBoxPlugin.h`；
4. 重新编译主程序与**全部**插件，确认全部加载成功（首页无报错）；
5. 更新偏差台账（若因此产生临时不一致）。

## 8. 手工验证脚本

位置 `scripts/verify/`，按验证目标命名，长驻保留：

| 脚本 | 覆盖范围 |
| --- | --- |
| `verify_whitespace.ps1` | 空白与编码：全仓 LF、末尾换行、无行尾空白、源码无制表符、`*.ps1` 带 BOM（§3.1，可纳入 CI） |
| `verify_format.ps1` | 格式：全部 C++ 源文件与 `.clang-format` 一致（`clang-format --dry-run --Werror`，只读不写）（§3.1，可纳入 CI） |
| `verify_naming.ps1` | 命名：`m_` / `s_` / `g_` / `k` 前缀与大小写（clang-tidy `readability-identifier-naming`，工具钉在 LLVM 22.1 线上）。**需先构建**，所以在 CI 里排在构建之后（§3.2） |
| `verify_conventions.ps1` | 可机械判定的编码规范：旧式 `SIGNAL()/SLOT()`、`QString("字面量")`、跨层 include、裸字符串 QSettings 键、头文件缺 `#pragma once`、`#include "Xxx.moc"` 之后还有代码（可纳入 CI） |
| `verify_docs.ps1` | 文档一致性：所有纳入版本控制的 `*.md`（含根目录 `README.md`）相对链接目标存在、`#锚点` 能落到标题、`docs/` 内无孤立文档（§10 第 5 条，可纳入 CI） |
| `verify_filesize.ps1` | 单文件规模：源码超过 600 行且未在脚本内的豁免表登记即失败，豁免须写明理由、结论所在文档与自己的上限（coding-standards §2 / §12，可纳入 CI） |
| `verify_shell.ps1` | 外壳冒烟：插件装载数量、主程序版本号、Qt 对话框中文翻译。`-Exe` 可指向别处的构建产物（验收打包结果，§9）；`-Lang en\|zh` 断言对应语言的界面（§3.3） |
| `verify_recent.ps1` | 收藏 / 最近使用 / 配置持久化 / 搜索 |
| `verify_videodl.ps1` | 视频下载插件的界面与状态 |
| `verify_videodl2.ps1` | 视频下载插件的界面与状态（补充场景） |
| `verify_videodl_download.ps1` | 下载链路 |
| `verify_videodl_fetch.ps1` | 内核（yt-dlp / ffmpeg）下载与安装 |
| `verify_videodl_reuse.ps1` | 内核常驻复用，不重复下载 |
| `verify_videodl_platform.ps1` | 各站点（B站 / YouTube / 抖音）适配 |
| `verify_videodl_status.ps1` | 状态栏与进度反馈 |
| `verify_videodl_logread.ps1` | 日志解析与输出读取 |

`scripts/` 下还有两个不按「验证目标」命名的辅助脚本，一并记在这里免得找不到：
`build_verify.ps1`（本机全量：清构建目录 → 配置 → 双配置构建 → 双配置 `ctest`，是
「CI 那套」的本地等价物）与 `lupdate_ts.ps1`（刷新译文骨架，见 §3.3）。

`fixtures/` 只放固定样本（各类分享文案、地址样例）。脚本运行时的截图与下载产物落在
`build/shots/`、`build/` 下的临时目录 —— 那是可再生成的东西，不进版本控制，跑完随手清掉。

## 9. 发布

交付形态是一个「解压即用」的 zip，由 CPack 产生。**顺序不能颠倒**：

> 第 1、2 步的检查、构建与测试，加上打包，CI 都会在干净机器上跑一遍
> （[§11](#11-持续集成ci)）。但 CI **不覆盖**第 7 步的冒烟，也不覆盖界面与真实下载
> 链路 —— 那几项只能靠人。

```powershell
# 1) 五个不依赖构建、秒级的检查，都返回 0 才继续
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_whitespace.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_format.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_conventions.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_filesize.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_docs.ps1
#    命名检查要 build/compile_commands.json，所以只能排在构建之后

# 2) 构建 + 把 Qt 运行时收进输出目录（Qt 的 DLL 靠这一步产生，缺了就打不出可用的包）
cmake --build --preset release
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_naming.ps1
cmake --build build --config Release --target deploy

# 3) 打包
cpack --config build\CPackConfig.cmake -C Release -B build\package
```

完整清单：

1. 跑上面第 1 步的五个脚本，构建之后再补跑命名检查
   （[§3.2](#32-命名检查clang-tidy)）：六个都返回 0 才继续；
2. 按 §6 确认版本号单一来源 —— 打包配置里没有再写一份版本号，
   `CPACK_PACKAGE_VERSION` 取的就是顶层 `project(... VERSION ...)`；
3. 构建 + `deploy`（见上面第 2 步）；
4. 确认 `build/bin/Release/translations/` 下有 `qt_zh_CN.qm`（deploy 目标的
   `--translations zh_CN` 负责，`main.cpp` 负责装载）—— 缺了它中文界面里的
   Qt 自带对话框按钮会是英文；
5. **过一遍 [偏差台账](./architecture.md#9-偏差台账)**，逐条确认「接受」的理由仍然成立，
   能关掉的条目关掉并更新文档；
6. 打包（见上面第 3 步）。两条保护在 `install()` 里，打错配置或忘了 deploy 会直接
   报错停下，不会产出一个缺 DLL 的包；
7. **解压产物跑一次冒烟测试** —— 只检查 zip 里有没有文件是不够的，要证明它能跑：

   ```powershell
   # 解压到任意目录，然后指给 verify_shell.ps1（§8）
   powershell -ExecutionPolicy Bypass -File scripts\verify\verify_shell.ps1 `
              -Exe <解压目录>\ToolBox-<版本>-win64\ToolBox.exe
   ```

   这一步同时验证了插件 DLL、Qt 运行时与 `translations/` 确实都在包里
   （按钮显示「确定」就说明翻译生效）；
8. 分发。实际做法是**打 `v*` 标签**（如 `v0.1.0`）把上面这套再让 CI 跑一遍，
   然后把**该标签那一次构建**的产物挂到 GitHub Release 上：

   ```powershell
   git tag -a v0.1.0 -m "ToolBox v0.1.0"
   git push origin v0.1.0
   gh run watch <该标签触发的 run id> --exit-status
   gh run download <run id> -n <artifact 名> -D build\release-assets
   gh release create v0.1.0 build\release-assets\*.zip --title "ToolBox v0.1.0" --notes "<发布说明>"
   ```

   用标签那一次的产物、而不是手上现成的 zip，是为了让「发出去的二进制」和
   「CI 验证过的那个提交」是同一个东西。CI 里那份 artifact 只保留 14 天，
   Release 附件才是长期可下载的交付物。

两件与接收方有关的事，交付时要说明：

- **MSVC 运行时是 app-local 的**：deploy 用 `--no-compiler-runtime` 跳过了
  `vc_redist.x64.exe`（17.9 MB 的安装器），改为把 CRT 的 DLL 复制到程序旁边
  （约 1.6 MB）。接收方**不需要再跑任何安装器**。目录名随工具集版本变
  （VS 18 是 `Microsoft.VC145.CRT`、VS 2022 是 `VC143`），所以用 glob 匹配；
  找不到就**让 deploy 失败**，而不是产出一个「接收方一运行就缺 DLL」的包。
- **包大小**：zip 约 20 MB、解压后约 47 MB。与早期相比砍掉三块：`opengl32sw.dll`
  （19.7 MB 软件 OpenGL，纯 Widgets 用光栅引擎，用 `--no-opengl-sw` 去掉）、
  `vc_redist.x64.exe`（17.9 MB，见上）、以及不再需要它带来的体积。
  **还剩 `dxcompiler.dll` + `dxil.dll` 共 15.1 MB**：它们是 Qt 运行期按需加载的
  （已用 `dumpbin /dependents` 确认不在 `Qt6Gui.dll` 的导入表里），只在走 D3D
  渲染路径时才需要。删掉它们不是 windeployqt 支持的开关，而「永远不走那条路径」
  我们没法完全证明，所以**保留**，把测量结果留在这里供以后决定。
- 若 `tools/bin/` 下已下载外部内核（yt-dlp / ffmpeg，合计约 177 MB），它们也会被打进
  包里 —— 这是有意为之：架构 §8 约定「整个 `bin/<Config>/` 拷走即可运行」。
  想要瘦身就先删掉 `tools/bin/` 再打包，插件会按需重新下载。

## 10. 文档维护规则（防漂移条款）

这是本文档存在的根本目的，条款必须严格执行：

1. **先改文档，后改代码。** 以下任一情况发生时，必须同一个提交里先更新对应文档章节：
   - 新增 / 删除 / 重命名插件目录 → `architecture.md §2`、本文档 §4
   - 改动 `sdk/ToolBoxPlugin.h` → `architecture.md §4` 与本文档 §7
   - 改变层间依赖方向或新增一层 → `architecture.md §2`、`§3`
   - 改变输出布局或插件目录约定 → `architecture.md §8`、本文档 §3
   - 改变交付形态或打包方式 → `architecture.md §8`、本文档 §9
   - 新增 / 改动 QSettings 键 → `architecture.md §6`
   - 引入新的第三方依赖或新的 Qt 模块 → `architecture.md §1`、本文档 §1
   - 拆分 / 合并架构组件 → `architecture.md §10`
2. **临时不遵守规范必须登记。** 任何「这次先这么写」的决定，都要写进偏差台账，
   注明原因、决定与计划；**没有登记的例外视为缺陷**。
3. **文档与代码冲突时以文档为准**，然后修代码；如果确实是文档过时，
   先改文档再说，不允许「代码是对的、文档放着不管」。
4. **每次发版前做一次审计**：过一遍偏差台账，并按
   [coding-standards.md §12](./coding-standards.md#12-检查手段映射表)
   核对各条规则的保证手段是否仍然成立（工具是否配置、是否在 CI 里跑）。
5. **文档之间的引用由脚本兜底**。章节会重编号、文件会挪位置，参考链接烂掉时没人会
   发现。`scripts/verify/verify_docs.ps1`（§8）机械检查三件事：相对链接的目标文件是否
   存在、`#锚点` 是否真能落到某个标题上、有没有哪份文档谁都不引用。新加文档时记得从
   至少一处链过去，否则脚本会判它「孤立」。扫描范围包含仓库根目录的 `README.md`，
   它同样会链接到 `docs/`；它不受「孤立」那条约束 —— 入口本来就没有人链它。
6. **`README.md` 只做入口，不复制 `docs/` 的内容**。它回答「这是什么、怎么跑起来、
   加工具从哪下手、文档在哪」，规则与契约一律链过去。写成第二份架构文档，就会出现
   两处需要同步的描述，而 `docs/` 才是权威来源。
7. 文档只描述**结构、契约、规则**，不抄写代码细节，不列举会在代码里变化的清单
   （工具数量、行数等），避免文档随代码频繁失效。

## 11. 持续集成（CI）

仓库在 GitHub：`GaussAA/tools-box`（公开）。配置是 [.github/workflows/ci.yml](../.github/workflows/ci.yml)，
在 push 到 `main`、推 `v*` 标签、开 PR、以及手动触发时各跑一遍。**打标签也跑**，
是为了让发布用的产物来自 CI 对该标签的构建，而不是把手边的 zip 碰巧发出去。

**它跑的就是本文档里那一套，不是另写一份。** 顺序是「快检 → 构建 → 测试 → 命名 →
打包」：快检只要几秒，失败得最快，能省掉后面几分钟的构建；命名检查反过来，必须等
构建产出 `compile_commands.json`。

| 阶段 | 内容 | 本地等价命令 |
| --- | --- | --- |
| 快检 | §3.1 / §8 的四个脚本，外加钉版的 clang-format 与 clang-tidy | `verify_whitespace` / `verify_format` / `verify_conventions` / `verify_docs` |
| 构建 | `cmake --preset default` + Debug / Release 双配置 | §3 的同一套 |
| 测试 | `ctest -C Debug` **与** `-C Release` | §5 |
| 命名 | `verify_naming.ps1`（排在构建之后，见下） | §3.2 |
| 打包 | `deploy` + `cpack`，产物作为 artifact 上传（保留 14 天） | §9 第 2、3 步 |

几个刻意的决定，改动 CI 前先读：

- **快检显式用 Windows PowerShell 5.1**（`powershell` 而不是 runner 默认的 pwsh 7）。
  脚本是按 5.1 的脾气写的（§3.1 的 BOM 约定），用较老的解析器验，覆盖更严。
- **Qt 用官方源，不开 mirror**。本项目吃过镜像的亏（§1 的三条禁令），CI 里也不要开。
- **两个 clang 工具都用 pip 装钉死的版本**（`clang-format==22.1.3` 与
  `clang-tidy==22.1.8`），不用 runner 上 VS 自带那个：镜像里的版本会滚动，检查会从
  「确定」变成「碰运气」。两者的钉法不同，理由见 §3.2 —— clang-format 必须与
  `verify_format.ps1` 的 `$expectedVersion` **精确**一致（输出逐字节敏感），
  clang-tidy 钉的是 LLVM 22.1 这条**线**（本机 VS 自带的 22.1.3 同样通过），
  由脚本自己校验，版本不在线上直接 FAIL。
- **复用 `CMakePresets.json`**，不在 workflow 里重复一份构建配置。命令行 `-D` 会
  覆盖 preset 的 `cacheVariables`（实测确认），所以 Qt 路径按 runner 的实际情况传，
  preset 里那份本机绝对路径保持不动。
- **Action 保持在大版本号的最新档**。Action 自带运行时会被 GitHub 淘汰（第一次跑就
  收到过 Node.js 20 的弃用告警），钉在旧大版本上迟早会红。升级前用 `gh api` 读它的
  `action.yml` 核对 inputs 有没有改名，别猜。
- **MSVC 那段是自己写的，不是第三方 Action**。原先用 `ilammy/msvc-dev-cmd`，但它的
  `action.yml` 声明 `using: node20` 且只有 v1.x，那条弃用告警只能靠不用它来消除。
  它做的事就是「跑 vcvars、把环境变量传给后续步骤」，所以改成 `vswhere` +
  微软自带的 `Microsoft.VisualStudio.DevShell.dll`：只导出相对调用前**新增或改变**
  的变量（本机实测 38 个），避免把 `GITHUB_*` / `RUNNER_*` 回写进 `GITHUB_ENV`；
  多行值会直接让这一步报错，因为 `GITHUB_ENV` 是「一行一个 KEY=VALUE」。这一步必须
  用 `pwsh`（PowerShell 7）：5.1 写 UTF-8 会带 BOM，会污染 `GITHUB_ENV`。
  代价是这段要自己维护，换来少一个第三方依赖、且不再有运行时弃用告警。
- **界面与真实下载链路不进 CI**。它们靠 UI 自动化驱动真实窗口和真实网络，属于
  手工回归（§5），不是单元测试的替代品。
- **命名检查排在构建之后**，不混在快检里：它读 `build/compile_commands.json` 和
  AUTOMOC 生成的 `.moc`，在未构建的干净机器上跑会缺文件；脚本把这种情况当硬失败，
  所以放错位置是「红」，而不是悄悄给个绿。

CI 覆盖不到的部分，仍然只能靠人：§8 里那些界面 / 下载链路的脚本、以及 §9 第 7 步
「解压产物跑一次冒烟」。**CI 绿不等于可以发布。**

## 相关文档

- [architecture.md](./architecture.md) —— 分层、职责边界、插件契约、偏差台账
- [coding-standards.md](./coding-standards.md) —— 命名、内存、信号槽等编码规则
