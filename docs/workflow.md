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
| 源码编码 | UTF-8（无 BOM），换行 LF | 源码含中文，必须 `/utf-8` |
| 格式化 | clang-format 22.1.3，随 Visual Studio 提供，**不必单独安装**；配置见根目录 `.clang-format` | 只作风格参考，不强制，见 §3.1 |

Qt 安装相关的已知坑（历史踩过的，不要再试）：
- **不要**用清华镜像、华为云 `repo.huaweicloud.com/qt`、阿里云镜像安装 Qt：
  前者包已损坏会报 `ArchiveChecksumError`，后者无法正确定位 XML 文件。
- 用 aqt 安装时保持官方源默认并发设置，改并发可能导致安装程序空转。

## 2. 源码树里什么进版本控制

进版本控制：`CMakeLists.txt`、`CMakePresets.json`、`docs/`、`scripts/`、
`sdk/`、`app/`、`plugins/`、`tests/`，以及三份格式约定 `.clang-format`、
`.editorconfig`、`.gitattributes`。

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

风格规则分两层，**一层能强制、一层只描述**，不要混为一谈。

**能强制的一层**：`.editorconfig` 里与编辑器无关的那几条 —— UTF-8、LF、文件末尾
恰好一个换行、不留行尾空白、源码不用制表符。检查手段是

```powershell
powershell -ExecutionPolicy Bypass -File scripts\verify\verify_whitespace.ps1
```

它扫全部纳入版本控制的文本文件，有违规就打印文件名与规则并返回非零退出码。

**只描述、不强制的一层**：`.clang-format`（大括号位置、100 列、指针符号、缩进、
注释不重排）。它的用途是统一人写代码时的判断，不是拿去批量重排。

本机 clang-format 来自 Visual Studio，路径
`C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe`，
空跑命令：

```powershell
$cf = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\clang-format.exe"
& $cf --dry-run --Werror (git ls-files "*.cpp" "*.h")
```

**实跑结论（P2）：不要让这份配置变成强制检查。** 实测它会让 14 个文件、约 330 行
发生改动，原因有二：

1. clang-format 没有「保留手工换行」的选项，也按字符数而不是中文双宽算列宽，
   于是现有代码里手工折断的长调用、手工对齐的 lambda 实参都会被重排；
2. 它只有一个全局 `AfterEnum` 开关，无法同时表达「短枚举写成一行、长枚举大括号
   另起一行」—— 而现有代码两种都在用。

所以：**保持描述性，不执行全仓格式化**。将来若要强制，必须先单独提一个「只做格式
归一化」的提交，并把该提交的 hash 登记进 `.git-blame-ignore-revs`，再把上面的
`--dry-run --Werror` 接进 CI。取舍记在
[architecture.md §9](./architecture.md#9-偏差台账) 的偏差 9.9。

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
| `verify_whitespace.ps1` | 空白与编码：全仓 LF、末尾换行、无行尾空白、源码无制表符（§3.1，可纳入 CI） |
| `verify_shell.ps1` | 外壳冒烟：插件装载数量、主程序版本号、Qt 对话框中文翻译 |
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

1. 跑 `scripts\verify\verify_whitespace.ps1`，空白与编码违规不许进版本；
2. 按 §6 确认版本号单一来源；
3. `cmake --build --preset release` + `deploy` 目标；
4. 确认 `build/bin/Release/translations/` 下有 `qt_zh_CN.qm`（deploy 目标的
   `--translations zh_CN` 负责，`main.cpp` 负责装载）—— 缺了它中文界面里的
   Qt 自带对话框按钮会是英文；
5. 确认 `build/bin/Release/` 下包含：`ToolBox.exe`、`tools/*.dll`、
   `tools/bin/`（外部内核，若已下载）、Qt 运行时与平台插件；
6. **过一遍 [偏差台账](./architecture.md#9-偏差台账)**，逐条确认「接受」的理由仍然成立，
   能关掉的条目关掉并更新文档；
7. 整目录分发，接收方无需安装 Qt 或 Python。

## 10. 文档维护规则（防漂移条款）

这是本文档存在的根本目的，条款必须严格执行：

1. **先改文档，后改代码。** 以下任一情况发生时，必须同一个提交里先更新对应文档章节：
   - 新增 / 删除 / 重命名插件目录 → `architecture.md §2`、本文档 §4
   - 改动 `sdk/ToolBoxPlugin.h` → `architecture.md §4` 与本文档 §7
   - 改变层间依赖方向或新增一层 → `architecture.md §2`、`§3`
   - 改变输出布局或插件目录约定 → `architecture.md §8`、本文档 §3
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
5. 文档只描述**结构、契约、规则**，不抄写代码细节，不列举会在代码里变化的清单
   （工具数量、行数等），避免文档随代码频繁失效。

## 相关文档

- [architecture.md](./architecture.md) —— 分层、职责边界、插件契约、偏差台账
- [coding-standards.md](./coding-standards.md) —— 命名、内存、信号槽等编码规则
