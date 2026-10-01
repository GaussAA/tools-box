# 工具箱 · 工程流程规范

> 状态：生效中 · 最后修订：2026-10-01
>
> 本文件规定「怎么做事」：环境、构建、加插件、测试、版本、发布，以及
> **文档与代码如何保持一致**。架构见 [architecture.md](./architecture.md)，
> 编码规则见 [coding-standards.md](./coding-standards.md)。

## 1. 环境与工具链

| 项目 | 约定 | 依据 |
| --- | --- | --- |
| 编译器 | MSVC 2022 (v143) 64-bit | 与 Qt 预编译包一致 |
| Qt | 6.10.3 `msvc2022_64`，路径 `D:/Qt/6.10.3/msvc2022_64` | `CMakePresets.json` |
| CMake | ≥ 3.21 | 顶层 `CMakeLists.txt` |
| 生成器 | Ninja Multi-Config，构建目录固定 `build/` | `CMakePresets.json` |
| 打包 | CPack（随 CMake 提供，不必单独安装），产物为 zip | 顶层 `CMakeLists.txt`；流程见 §9 |
| 源码编码 | UTF-8，换行 LF；`*.ps1` 必须带 BOM，其余文件不带 BOM（原因见 §3.1） | 源码含中文，必须 `/utf-8` |
| 警告等级 | `/W4 /permissive- /WX`，**警告即错误**，Debug / Release 均须 0 告警 | 顶层 `CMakeLists.txt`；规则见 coding-standards §9 |
| 格式化 | clang-format 22.1.3，随 Visual Studio 提供，**不必单独安装**；配置见根目录 `.clang-format` | **已强制**：全仓已归一化，`verify_format.ps1` 把关，见 §3.1 |
| 静态检查 | clang-tidy 22.1.3，同样随 Visual Studio 提供；配置见根目录 `.clang-tidy` | 目前手动跑，不接入构建，见 §3.2 |

Qt 安装相关的已知坑（历史踩过的，不要再试）：
- **不要**用清华镜像、华为云 `repo.huaweicloud.com/qt`、阿里云镜像安装 Qt：
  前者包已损坏会报 `ArchiveChecksumError`，后者无法正确定位 XML 文件。
- 用 aqt 安装时保持官方源默认并发设置，改并发可能导致安装程序空转。

## 2. 源码树里什么进版本控制

进版本控制：`CMakeLists.txt`、`CMakePresets.json`、`docs/`、`scripts/`、
`sdk/`、`app/`、`plugins/`、`tests/`，以及五份工具配置 `.clang-format`、
`.clang-tidy`、`.editorconfig`、`.gitattributes`、`.git-blame-ignore-revs`。

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

### 3.2 命名检查（clang-tidy，目前手动跑）

[clang-format 管不了命名](#31-代码风格检查)，命名规则（[coding-standards.md §1](./coding-standards.md#1-命名)）
靠根目录 `.clang-tidy` + clang-tidy 的 `readability-identifier-naming` 检查。clang-tidy
同样随 Visual Studio 提供，**不必单独安装**：

```
C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe
```

**它需要一份 `compile_commands.json`**，而本工程用的 Ninja Multi-Config 生成器默认
不生成。所以单独配置一个只含 Debug 的目录，并**先构建一次**：

```powershell
cmake -S . -B build/tidy -G "Ninja Multi-Config" `
      -DCMAKE_PREFIX_PATH="D:/Qt/6.10.3/msvc2022_64" `
      -DCMAKE_CONFIGURATION_TYPES=Debug `
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/tidy --config Debug

$ct = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-tidy.exe"
& $ct -p build/tidy (git ls-files "*.cpp")
```

两个必须照做的细节，否则结果不可信：

1. **只配 Debug**。同时存在 Debug/Release 两套条目时，clang-tidy 只会挑其中一条，
   而另一套的 AUTOMOC 目录是空的，会出现假的 `'Xxx.moc' file not found`。
2. **必须先构建一次**。`#include "Xxx.moc"` 指向的是构建期由 AUTOMOC 生成的文件，
   没构建过这些文件就不存在。缺少它们的那 7 个文件（2 个插件入口 + 5 个测试）会被
   跳过分析。

`.clang-tidy` 只开白名单，刻意**不含** `cppcoreguidelines-owning-memory`：它要求 `new`
出来的裸指针必须赋给 `gsl::owner<>`，与本项目（及 Qt 整体惯例）用父子对象树表达所有权
的写法正面冲突，全仓实跑 61 条命中**全是误报**。所有权一条继续靠评审。

**结论（P3）**：命名规则**当前全仓 0 命中** —— 16 个 `.cpp` 加上 `HeaderFilterRegex`
覆盖的头文件，没有任何一处违反 §1。这条结论做过反向验证：手工把
`ToolCatalog.cpp` 里的一个局部变量改成 `HasVisibleChild_XX`，检查立刻报出
`invalid case style for local variable`。

**目前不接入构建、也不写成 `scripts/verify/` 脚本**：它要先配置再完整构建一个独立
目录（分钟级），与 §8 里那些「秒级、只读、不构建」的验证脚本定位不同；而 CI 尚不可
接入（仓库没有远端）。引入 CI 后，这里应成为 CI 的一步，届时再评估把它和
[§3.1](#31-代码风格检查) 的两条一起接进去。

## 4. 新增一个工具插件

按顺序做，每一步都有明确产出：

1. 复制 `plugins/base64/` 为新目录，目录名全小写（如 `plugins/regextool/`）；
2. 重命名 `Base64Plugin.*` 为新插件名，同步改类名与 `CMakeLists.txt` 里的目标名；
3. 在 `meta()` 中填写：`id`（遵守 [architecture.md §4.1](./architecture.md#41-工具标识)
   的格式与不可变约定）、`name`、`category`、`version`、`description`；
4. 需要图标就放 `icons/*.svg` 并在 `CMakeLists.txt` 里加 `qt_add_resources`；
   不需要图标则留空，外壳会自动生成首字占位图标；
5. 在顶层 `CMakeLists.txt` 中 `add_subdirectory(plugins/<新目录>)`；
6. 页面需要持久化则实现 `IToolPage`，键名只用裸名，由 `ToolSettings` 加前缀；
7. 纯逻辑（解析、转换、过滤）必须放进独立的无 UI 依赖的类或自由函数，
   并补 Qt Test 用例（见 §5）；
8. 更新本文档 §8 的脚本清单（若新增了手工验证脚本）。

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
| `tst_outputparsing` | [videodl/core/OutputParsing.*](../plugins/videodl/core/OutputParsing.h)：输出解码、剥色、地址提取、进度/阶段/产物解析 |
| `tst_douyinsupport` | [videodl/core/DouyinSupport.*](../plugins/videodl/core/DouyinSupport.h)：站点判定、画质档位、DOM 字段提取 |
| `tst_cookiefile` | [videodl/core/CookieFile.*](../plugins/videodl/core/CookieFile.h)：cookies 规范化与各失败分支 |
| `tst_enginelocator` | [videodl/core/EngineLocator.*](../plugins/videodl/core/EngineLocator.h)：内核定位顺序 |

新增用例：在 `tests/` 下加一个 `tst_<模块名>.cpp`，用
`toolbox_add_test(tst_<模块名> <被测静态库>)` 注册，并同步本表。

约定：

- 只用 `QTEST_APPLESS_MAIN`（`QCoreApplication` 级），**不允许**依赖 `QApplication`。
  一旦发现非要后者不可，说明逻辑还挂在控件上，应先把逻辑挪进 `core/`。
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
| `verify_conventions.ps1` | 可机械判定的编码规范：旧式 `SIGNAL()/SLOT()`、`QString("字面量")`、跨层 include、裸字符串 QSettings 键、头文件缺 `#pragma once`、`#include "Xxx.moc"` 之后还有代码（可纳入 CI） |
| `verify_docs.ps1` | 文档一致性：相对链接目标存在、`#锚点` 能落到标题、无孤立文档（§10 第 5 条，可纳入 CI） |
| `verify_shell.ps1` | 外壳冒烟：插件装载数量、主程序版本号、Qt 对话框中文翻译。`-Exe` 可指向别处的构建产物，用于验收打包结果（§9） |
| `verify_recent.ps1` | 收藏 / 最近使用 / 配置持久化 / 搜索 |
| `verify_videodl.ps1` | 视频下载插件的界面与状态 |
| `verify_videodl2.ps1` | 视频下载插件的界面与状态（补充场景） |
| `verify_videodl_download.ps1` | 下载链路 |
| `verify_videodl_fetch.ps1` | 内核（yt-dlp / ffmpeg）下载与安装 |
| `verify_videodl_reuse.ps1` | 内核常驻复用，不重复下载 |
| `verify_videodl_platform.ps1` | 各站点（B站 / YouTube / 抖音）适配 |
| `verify_videodl_status.ps1` | 状态栏与进度反馈 |
| `verify_videodl_logread.ps1` | 日志解析与输出读取 |

`fixtures/` 只放固定样本（各类分享文案、地址样例）。脚本运行时的截图与下载产物落在
`build/shots/`、`build/` 下的临时目录 —— 那是可再生成的东西，不进版本控制，跑完随手清掉。

## 9. 发布

交付形态是一个「解压即用」的 zip，由 CPack 产生。**顺序不能颠倒**：

```powershell
# 1) 四个不依赖界面、秒级的检查，都返回 0 才继续
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_whitespace.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_format.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_conventions.ps1
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_docs.ps1

# 2) 构建 + 把 Qt 运行时收进输出目录（Qt 的 DLL 靠这一步产生，缺了就打不出可用的包）
cmake --build --preset release
cmake --build build --config Release --target deploy

# 3) 打包
cpack --config build\CPackConfig.cmake -C Release -B build\package
```

完整清单：

1. 跑上面第 1 步的四个脚本；命名检查（[§3.2](#32-命名检查clang-tidy目前手动跑)）
   要单独配置并构建一个目录，不强制拦在发版路径上，但改动过命名相关的代码后应当跑一次；
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
8. 分发 `build/package/ToolBox-<版本>-win64.zip`。

两件与接收方有关的事，交付时要说明：

- **MSVC 运行时**：Qt 的 DLL 依赖 `msvcp140.dll` / `vcruntime140.dll`，包里没有
  app-local 版本，只有 deploy 顺带放进来的 `vc_redist.x64.exe`。接收方若缺运行时，
  先跑一次它。
- **包大小**：zip 约 44 MB（其中 `opengl32sw.dll` 20 MB、`dxcompiler.dll` 14 MB 是
  windeployqt 带上的）。若 `tools/bin/` 下已下载外部内核（yt-dlp / ffmpeg，合计约
  177 MB），它们也会被打进包里 —— 这是有意为之：架构 §8 约定「整个 `bin/<Config>/`
  拷走即可运行」。想要瘦身就先删掉 `tools/bin/` 再打包，插件会按需重新下载。

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
   至少一处链过去，否则脚本会判它「孤立」。
6. 文档只描述**结构、契约、规则**，不抄写代码细节，不列举会在代码里变化的清单
   （工具数量、行数等），避免文档随代码频繁失效。

## 相关文档

- [architecture.md](./architecture.md) —— 分层、职责边界、插件契约、偏差台账
- [coding-standards.md](./coding-standards.md) —— 命名、内存、信号槽等编码规则
