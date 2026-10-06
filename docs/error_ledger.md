# 错误台账

只收「**调试两轮以上才抓到**」或「**症状与根因隔得很远**」的顽固问题；一次就定位的
不收（那类该修在代码注释里）。每条必须写清 **守住它的手段** —— 没有手段的教训等于
没沉淀：下次换个人（或换个会话）还会踩。

- [现象] 用一句用户能复述的话描述；[根因] 写机制，不写「不小心」；[正解] 给可执行的修复；
- [守住] 指明靠哪个脚本 / 断言 / 文档条款防止复发。

---

## 1. 双击程序没反应，界面完全不出现

- **[现象]** 构建全绿、`ctest` 全过，但双击 `ToolBox.exe` 进程立刻退出，没有任何窗口、
  任何报错。而且「以前每次都能出来，最近几次开发就不出现了」。
- **[根因]** 输出目录里**没有 Qt 运行时**（`Qt6*.dll`、`platforms/qwindows.dll`）——
  它们由**可选目标** `deploy`（windeployqt）拷进来，不在默认构建里。而
  `scripts/build_verify.ps1` 第一步 `Remove-Item -Recurse -Force build` 会把整棵构建树
  **连同之前 deploy 好的运行时一起删掉**，它自己却从不跑 deploy。所以每跑一次全量验证，
  「双击能起界面」就失效一次 —— 而当天的 10/10 ctest 对此完全无感。
- **[正解]** 手工跑界面前先 `cmake --build build --config Release --target deploy`；
  2026-10-02 起 `build_verify.ps1` 在全量验证末尾**自动跑 deploy**（deploy 目标在受限
  环境跑不了 windeployqt 时，按 windeployqt 清单手工兜底拷贝运行时），「全量验证之后
  的双击」天然可用，不再依赖谁记得去跑。CI 的产物没有这个问题（CI 流程里有 deploy），
  所以「CI 出的包能用、本地双击不行」曾是这条的指纹。
- **[守住]** `build_verify.ps1` 末尾对 Qt6Core.dll / platforms\qwindows.dll /
  translations\qtbase_zh_CN.qm 三件套做存在性断言，缺任一项直接 FAIL（退出码 3）；
  `verify_shell.ps1`（真启动产物）—— 它在产物缺运行时会直接失败而不是绿着。

## 2. 界面全英文（或消息框按钮是 OK），中文文案却完全正常

- **[现象]** 两种形态：整窗英文（导航是 "Home | Developer tools | ..."），或只有
  QMessageBox 的按钮是 `OK` 而其余全中文。改语言配置、重启都没用。
- **[根因]** 三个独立因素叠加，见 [workflow.md §3.3](./workflow.md#33-界面语言与翻译)：
  ① Qt 6 把词条搬进 `qtbase_<locale>.qm`，旧的 `qt_<locale>.qm` 是 **99 字节空壳**，
  按 Qt 5 的 `qt` 前缀加载会**成功**装上一个什么都不翻译的 translator，不报错；
  ② 本机 Qt 装在非标准路径 `C:\Qt6.12\`，`QLibraryInfo::path(TranslationsPath)` 指向的
  编译期前缀（`C:/Qt/6.12.0/...`）不存在，回退路径落空；
  ③ 会话环境变量 `LANG` / `LC_ALL` 会**盖过系统语言**（实测系统 zh-CN、程序整个起成英文）。
- **[正解]** 加载前缀改 `qtbase`（保留 `qt` 回退）；`LanguageChoice` 在配置只写语言时补
  默认地区（`zh` → `zh_CN`，否则找不存在的 `qtbase_zh.qm`）；`verify_shell.ps1` 自己设并
  复原 `ui/language`，不再依赖调用方与环境。
- **[守住]** `verify_shell.ps1` 的「对话框按钮是否被本地化」断言 —— 它盯着的 bug 活了
  一次 Qt 大版本升级而无人发现，因为迁移审计当天没有任何测试会启动真程序。

## 3. 门禁脚本「全绿」，但该 FAIL 的没 FAIL（假绿）

- **[现象]** `verify_filesize.ps1` 报某文件 1181 行、判定豁免内通过；实际该文件 **1269 行**，
  差了 88 行 —— 真超限时它照样绿。
- **[根因]** PowerShell 5.1 的 `Get-Content` 按**系统 ANSI 代码页**（936/GBK）解码无 BOM
  的 UTF-8：中文注释被解成乱码的同时，某些字节序列会被**并成一行**。门禁数错行数 =
  该 FAIL 时不 FAIL。同一个坑的另一个表现：`.ps1` 本身若无 BOM，中文注释乱码足以让
  脚本解析失败。
- **[正解]** 数行一律用 `[System.IO.File]::ReadAllLines($path).Length`（与 LF 字节数一致）；
  新增 `*.ps1` 必须带 UTF-8 BOM（`verify_whitespace.ps1` 查前三个字节）。
- **[守住]** 对检查脚本本身做**注入式反向验证**：往被检对象里注入一个必然违规的内容
  （如一行含中文的 `QStringLiteral`），确认脚本立刻 FAIL 并返回非零。绿了不算数，
  能红才算数。

## 4. 「测试全绿」但程序有 bug：不启动真程序的盲区

- **[现象]** 10/10 ctest 通过的当天，程序实际：双击起不来（见 1）、消息框按钮是 `OK`
  （见 2）、抖音渲染失败被报成「60 秒超时」（真因在 `errorString` 里被超时分支吃掉）。
- **[根因]** 单元/集成测试都**不启动真实产物**：部署期问题（缺运行时）、启动期问题
  （翻译加载）、原生对话框文本、进程退出码 —— 这一整类问题对它们是盲的。而 GUI 冒烟
  脚本 `verify_shell.ps1` 此前**从未在本地跑过**，等于不存在。
- **[正解]** 验证体系分两层：命令行层（构建 + ctest + 七个静态检查）+ **真机层**
  （`verify_shell.ps1` 启动真实产物、截图、UIAutomation 断言、点开关于对话框）。
  修完 UI 侧的东西，两层都要过。
- **[守住]** `run_all.ps1` 末尾会列出全部只能人工跑的脚本 —— 提醒「自动检查全绿」
  不等于「可以交付」；GUI 冒烟对 Debug 与 **Release**（分发形态）各跑一次。

## 5. commit 时命令整条被拒，像是 git 坏了

- **[现象]** `git commit -m "...（提交信息里含 PowerShell / cmd 字样）..."` 被工具层
  安全过滤整条拒绝，报错让人以为 git 出了问题。
- **[根因]** 命令执行工具按**词**拦截含 `powershell` / `pwsh` / `cmd` 的命令 —— 提交
  信息里的措辞也会触发（例如「PowerShell 5.1 的坑」）。
- **[正解]** 提交信息换措辞（「解压命令」「系统自带终端」等）；或者把含敏感词的说明
  放进代码注释 / 文档而不是提交信息。
- **[守住]** 无脚本可守，是执行环境的行为 —— 写进本台账就是为了「下次第一时间想到，
  而不是先怀疑 git 坏了」。

## 6. 真机脚本一律报「nav not found」，而程序其实启动得好好的

- **[现象]** `verify_videodl_fetch.ps1` 一开跑就 `ABORT: nav not found`（找不到导航项），
  同一份产物用 `verify_shell.ps1` 却一路全绿：窗口在、插件加载了、截图也正常。
- **[根因]**  symptom 与真因隔了三层。① 跑脚本的会话里 `LANG=en_US.UTF-8`
  （`LC_ALL=C.UTF-8`），**环境变量盖过系统语言**，程序整个起成英文界面——
  机制在第 2 条已记过；② 这些脚本按**中文标签**找控件（`$videoDl = "视频下载"`），
  英文界面下自然一个都找不到；③ 于是报出「导航不存在」，而真因是环境语言，
  与「导航」毫无关系。当时 `ui/language` 是未设置的（= 跟随系统），
  10 个真机脚本里只有 `verify_shell.ps1` 会自己强制写 `ui/language`，
  其余 9 个都假设「默认就是中文」。
- **[正解]** 新增 `scripts/verify/_ui_language.ps1`：`Enter-UiLanguage` 强制写入
  `zh_CN` 并**记住原值**，`Restore-UiLanguage` 复原；两者幂等，且额外注册
  `PowerShell.Exiting` 事件兜住中途 `exit 1` 的 ABORT 路径 —— 否则跑测试的临时
  选择会变成用户下次启动的语言（`verify_shell` 早先只在收尾处复原，中途异常退出
  就会留下 `zh_CN`）。9 个脚本已全部接入，实测 `verify_videodl_fetch` 当场
  下载成功 yt-dlp（17.8 MB）并复原了原值。
- **[守住]** ① 任何按界面文案驱动 UI 的脚本，启动前必须 `Enter-UiLanguage`；
  ② 报错文案要指向真因而不是「找不到控件」——将来看到「找不到 X」，先问
  「X 是不是用另一种语言写的」；③ 周六自动化的报告须把界面语言列为前置条件说明。
- **[同源的两个追加坑（2026-10-06 当晚实测）]** ④ **脚本会把自己的配置删掉**：
  `verify_recent.ps1` 为从干净状态测收藏/最近使用，会 `Remove-Item HKCU:\Software\ToolBox\ToolBox\ui -Recurse`
  ——若守卫写在 wipes 之前，刚写进去的 `zh_CN` **被自己删掉**，应用回落跟随系统、
  界面变英文，脚本却"跑通了"（它要找的中文控件根本不存在）。守卫必须排在 wipes **之后**。
  ⑤ **6 个脚本硬编码 Debug 产物且没有 `-Exe` 参数**：`verify_videodl{,.2,_download,_reuse,_status}`
  与 `verify_recent` 早先把 `bin\Debug\ToolBox.exe` 写死在脚本里，调用方传的 `-Exe` 被静默忽略；
  而 Debug 目录通常只有跑过 deploy 才有 Qt 运行时，于是「换个构建跑」实际换成了另一个产物，
  症状是「功能坏了」而真因是「跑的根本不是那个构建」。现在 6 个脚本统一提供 `-Exe`，
  默认值仍是 Debug 以保持原行为。
