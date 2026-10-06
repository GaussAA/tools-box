<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="en_US">
<context>
    <name>Base64Page</name>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="41"/>
        <source>在这里输入原文，或粘贴 Base64</source>
        <translation>Type the plain text here, or paste Base64</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="44"/>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="65"/>
        <source>结果</source>
        <translation>Result</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="47"/>
        <source>编码 →</source>
        <translation>Encode →</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="48"/>
        <source>← 解码</source>
        <translation>← Decode</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="49"/>
        <source>结果转原文</source>
        <translation>Use result as input</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="50"/>
        <source>清空</source>
        <translation>Clear</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="59"/>
        <source>URL 安全字符集（用 - _ 代替 + /）</source>
        <translation>URL-safe alphabet (use - _ instead of + /)</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="61"/>
        <source>原文</source>
        <translation>Plain text</translation>
    </message>
</context>
<context>
    <name>Base64Plugin</name>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="102"/>
        <source>Base64 编解码</source>
        <translation>Base64 Encode / Decode</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="103"/>
        <source>文本编码</source>
        <translation>Text encoding</translation>
    </message>
    <message>
        <location filename="../plugins/base64/Base64Plugin.cpp" line="105"/>
        <source>UTF-8 文本与 Base64 互转，支持 URL 安全字符集。</source>
        <translation>Converts between UTF-8 text and Base64, with URL-safe alphabet support.</translation>
    </message>
</context>
<context>
    <name>DouyinResolver</name>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="75"/>
        <source>抖音要靠浏览器渲染页面才能取到播放地址，但这台机器上没找到 Edge 或 Chrome。</source>
        <translation>Douyin needs a browser to render the page before the play URL appears, but neither Edge nor Chrome was found on this machine.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="77"/>
        <source>未找到可用的浏览器。</source>
        <translation>No usable browser was found.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="105"/>
        <source>抖音的播放地址要等页面脚本跑完才出现，正在用浏览器渲染…</source>
        <translation>Douyin’s play URL only appears after the page scripts run; rendering it in a browser…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="106"/>
        <source>渲染器：%1</source>
        <translation>Renderer: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="108"/>
        <source>正在渲染抖音页面…</source>
        <translation>Rendering the Douyin page…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="160"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="160"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="190"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="190"/>
        <source>已取消。</source>
        <translation>Cancelled.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="166"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="195"/>
        <source>无法启动浏览器：%1</source>
        <translation>Could not start the browser: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="166"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="195"/>
        <source>无法启动浏览器。</source>
        <translation>Could not launch the browser.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="170"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="198"/>
        <source>渲染超时：抖音页面没能在 60 秒内就绪。</source>
        <translation>Render timed out: the Douyin page did not become ready within 60 seconds.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="170"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="198"/>
        <source>抖音页面渲染超时。</source>
        <translation>Timed out rendering the Douyin page.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="174"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="201"/>
        <source>浏览器渲染失败（退出码 %1）。</source>
        <translation>Browser rendering failed (exit code %1).</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="174"/>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="201"/>
        <source>抖音页面渲染失败。</source>
        <translation>Failed to render the Douyin page.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="204"/>
        <source>页面渲染完了，但里面没有 video_id —— 多半是抖音又改版了。</source>
        <translation>The page rendered but contains no video_id — Douyin most likely changed its site again.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="205"/>
        <source>没能从抖音页面里取到播放地址。</source>
        <translation>Could not extract the play URL from the Douyin page.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="211"/>
        <source>已取到 video_id：%1</source>
        <translation>Got video_id: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="213"/>
        <source>标题：%1</source>
        <translation>Title: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DouyinResolver.cpp" line="217"/>
        <source>播放地址接口：%1</source>
        <translation>Play URL endpoint: %1</translation>
    </message>
</context>
<context>
    <name>DownloadRunner</name>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="79"/>
        <source>%1，剩余 %2</source>
        <translation>%1, %2 remaining</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="80"/>
        <source>正在下载… %1%</source>
        <translation>Downloading… %1%</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="81"/>
        <source>正在下载… %1%（%2）</source>
        <translation>Downloading… %1% (%2)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="85"/>
        <source>正在下载…</source>
        <translation>Downloading…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="91"/>
        <source>正在合并音视频…</source>
        <translation>Merging audio and video…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/DownloadRunner.cpp" line="95"/>
        <source>正在提取音频…</source>
        <translation>Extracting audio…</translation>
    </message>
</context>
<context>
    <name>EngineFetcher</name>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="70"/>
        <source>ffmpeg</source>
        <translation>ffmpeg</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="70"/>
        <source>yt-dlp</source>
        <translation>yt-dlp</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="89"/>
        <source>开始下载 %1 …</source>
        <translation>Starting download of %1 …</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="91"/>
        <source>正在下载 %1 …</source>
        <translation>Downloading %1 …</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="136"/>
        <source>无法写入临时文件：%1</source>
        <translation>Could not write the temporary file: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="177"/>
        <source>%1 下载中断，正在重试（第 %2 次）…</source>
        <translation>Download of %1 interrupted, retrying (attempt %2)…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="196"/>
        <source>正在下载 %1 … %2%（%3 / %4 MB）</source>
        <translation>Downloading %1 … %2% (%3 / %4 MB)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="212"/>
        <source>网络请求已取消</source>
        <translation>Network request cancelled</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="236"/>
        <source>服务器未支持断点续传，重新下载整个文件。</source>
        <translation>The server does not support resuming, so the whole file is downloaded again.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="237"/>
        <source>续传的起始位置与已下载的部分对不上，重新下载整个文件。</source>
        <translation>The resumed range does not match what is already on disk; restarting the whole file.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="243"/>
        <source>第 %1 次下载中断（%2），重试中…</source>
        <translation>Attempt %1 interrupted (%2), retrying…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="247"/>
        <source>下载失败（已尝试 %1 次）：%2</source>
        <translation>Download failed after %1 attempt(s): %2</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="269"/>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="291"/>
        <source>%1 下载失败。</source>
        <translation>Failed to download %1.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="283"/>
        <source>下载的 %1 文件头不对，不像是可用的文件，已丢弃，请重试。</source>
        <translation>The downloaded %1 has an unexpected file header and does not look usable. It has been discarded; please try again.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="285"/>
        <source>下载的 %1 只有 %2 字节，不像是完整文件，已丢弃，请重试。</source>
        <translation>The downloaded %1 is only %2 bytes, which does not look like a complete file. It has been discarded; please try again.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="293"/>
        <source>保存失败：%1</source>
        <translation>Could not save: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="296"/>
        <source>%1 保存失败。</source>
        <translation>Failed to save %1.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="303"/>
        <source>yt-dlp 已就绪：%1</source>
        <translation>yt-dlp ready: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="307"/>
        <source>yt-dlp 已就绪。</source>
        <translation>yt-dlp is ready.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="316"/>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="326"/>
        <source>正在解压 ffmpeg …</source>
        <translation>Extracting ffmpeg …</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="345"/>
        <source>解压失败：%1</source>
        <translation>Extraction failed: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="345"/>
        <source>未知错误</source>
        <translation>unknown error</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="346"/>
        <source>ffmpeg 安装失败：解压出错。</source>
        <translation>ffmpeg install failed: extraction error.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="370"/>
        <source>解压后没找到 ffmpeg.exe。</source>
        <translation>ffmpeg.exe was not found after extraction.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="371"/>
        <source>ffmpeg 安装失败：压缩包里没有 ffmpeg.exe。</source>
        <translation>ffmpeg install failed: the archive contains no ffmpeg.exe.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="377"/>
        <source>ffmpeg 已就绪：%1</source>
        <translation>ffmpeg ready: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="378"/>
        <source>ffmpeg 已就绪。</source>
        <translation>ffmpeg is ready.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="382"/>
        <source>复制 ffmpeg 失败：%1</source>
        <translation>Could not copy ffmpeg: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="383"/>
        <source>ffmpeg 安装失败：无法写入目标目录。</source>
        <translation>ffmpeg install failed: cannot write to the target folder.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="397"/>
        <source>无法调用 PowerShell 解压，请手动指定 ffmpeg 路径。</source>
        <translation>Could not run PowerShell to extract the archive; choose the ffmpeg path manually.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/EngineFetcher.cpp" line="405"/>
        <source>ffmpeg 安装失败：无法解压。</source>
        <translation>ffmpeg install failed: cannot extract the archive.</translation>
    </message>
</context>
<context>
    <name>JsonFormatPlugin</name>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="15"/>
        <source>JSON 格式化</source>
        <translation>JSON Formatter</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="16"/>
        <source>开发辅助</source>
        <translation>Developer tools</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="18"/>
        <source>格式化、压缩 JSON，并提示语法错误位置。</source>
        <translation>Format and minify JSON, and point out where syntax errors are.</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="29"/>
        <source>在这里粘贴 JSON</source>
        <translation>Paste JSON here</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="32"/>
        <source>结果</source>
        <translation>Result</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="37"/>
        <source>格式化</source>
        <translation>Format</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="38"/>
        <source>压缩</source>
        <translation>Minify</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="39"/>
        <source>清空</source>
        <translation>Clear</translation>
    </message>
    <message>
        <location filename="../plugins/jsonfmt/JsonFormatPlugin.cpp" line="59"/>
        <source>解析失败：偏移 %1 —— %2</source>
        <translation>Parse failed: offset %1 — %2</translation>
    </message>
</context>
<context>
    <name>MainWindow</name>
    <message>
        <location filename="../app/MainWindow.cpp" line="92"/>
        <location filename="../app/MainWindow.cpp" line="524"/>
        <source>工具箱</source>
        <translation>Toolbox</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="109"/>
        <source>搜索工具…</source>
        <translation>Search tools…</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="149"/>
        <source>文件</source>
        <translation>File</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="150"/>
        <source>重载插件</source>
        <translation>Reload plugins</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="151"/>
        <source>打开插件目录</source>
        <translation>Open plugin folder</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="155"/>
        <source>退出</source>
        <translation>Quit</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="157"/>
        <source>帮助</source>
        <translation>Help</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="162"/>
        <location filename="../app/MainWindow.cpp" line="187"/>
        <source>界面语言</source>
        <translation>Interface language</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="177"/>
        <source>跟随系统</source>
        <translation>Follow system</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="178"/>
        <source>中文</source>
        <translation>中文</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="179"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="187"/>
        <source>语言设置已保存，重启程序后生效。</source>
        <translation>Language preference saved. Restart the app to apply it.</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="191"/>
        <source>关于</source>
        <translation>About</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="223"/>
        <source>其他</source>
        <translation>Other</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="258"/>
        <source>已加载 %1 个工具 · 插件目录 %2</source>
        <translation>%1 tools loaded · plugin folder %2</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="309"/>
        <location filename="../app/MainWindow.cpp" line="311"/>
        <source>首页</source>
        <translation>Home</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="316"/>
        <source>收藏</source>
        <translation>Favorites</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="325"/>
        <source>最近使用</source>
        <translation>Recent</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="426"/>
        <source>没有匹配「%1」的工具</source>
        <translation>No tools match “%1”</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="428"/>
        <source>匹配到 %1 个工具</source>
        <translation>%1 tool(s) matched</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="503"/>
        <source>取消收藏</source>
        <translation>Remove from favorites</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="503"/>
        <source>加入收藏</source>
        <translation>Add to favorites</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="525"/>
        <source>已加载 &lt;b&gt;%1&lt;/b&gt; 个工具。</source>
        <translation>&lt;b&gt;%1&lt;/b&gt; tools loaded.</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="526"/>
        <source>在左侧右键任意工具，可以把它加进「收藏」。</source>
        <translation>Right-click any tool on the left to add it to Favorites.</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="528"/>
        <source>要收录新工具，把插件 DLL 放进下面的目录，再按 F5 重新加载：&lt;br&gt;&lt;code&gt;%1&lt;/code&gt;</source>
        <translation>To add a tool, drop its plugin DLL into the folder below and press F5:&lt;br&gt;&lt;code&gt;%1&lt;/code&gt;</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="539"/>
        <source>以下插件加载失败：</source>
        <translation>These plugins failed to load:</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="548"/>
        <source>关于工具箱</source>
        <translation>About Toolbox</translation>
    </message>
    <message>
        <location filename="../app/MainWindow.cpp" line="549"/>
        <source>&lt;b&gt;工具箱&lt;/b&gt; %1&lt;br&gt;&lt;br&gt;基于 Qt %2 构建的插件式桌面工具箱。&lt;br&gt;每个工具都是一个独立 DLL 插件，放进 tools 目录即可生效。</source>
        <translation>&lt;b&gt;Toolbox&lt;/b&gt; %1&lt;br&gt;&lt;br&gt;A plugin-based desktop toolbox built with Qt %2.&lt;br&gt;Every tool is a separate DLL plugin; drop one into the tools folder and it appears.</translation>
    </message>
</context>
<context>
    <name>ToolRegistry</name>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="35"/>
        <source>插件目录不存在：%1</source>
        <translation>Plugin folder does not exist: %1</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="59"/>
        <source>%1：不是兼容的工具箱插件（IID/接口版本不符，已跳过加载）。</source>
        <translation>%1: not a compatible toolbox plugin (IID / interface version mismatch; skipped).</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="74"/>
        <source>%1：工具 id「%2」不合法（应为「分类.工具名」，已跳过加载）。</source>
        <translation>%1: invalid tool id &quot;%2&quot; (expected &quot;category.tool-name&quot;); skipped.</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="84"/>
        <source>%1：构建环境与本程序不一致（插件 %2，本程序 %3），已跳过加载。</source>
        <translation>%1: built with a different toolchain (plugin %2, this app %3); skipped.</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="85"/>
        <source>未提供</source>
        <translation>not provided</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="95"/>
        <source>%1：工具 id「%2」与本目录中已加载的插件重复，已跳过加载。</source>
        <translation>%1: tool id &quot;%2&quot; duplicates a plugin already loaded from the same directory; skipped.</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="117"/>
        <source>%1：不是有效的工具箱插件（未实现 IToolPlugin）。</source>
        <translation>%1: not a valid toolbox plugin (does not implement IToolPlugin).</translation>
    </message>
    <message>
        <location filename="../app/ToolRegistry.cpp" line="136"/>
        <source>已加载插件：%1（%2 %3）</source>
        <translation>Loaded plugin: %1 (%2 %3)</translation>
    </message>
</context>
<context>
    <name>VideoDlPage</name>
    <message>
        <source>无法启动下载内核，请检查 yt-dlp 路径是否有效。</source>
        <translation type="vanished">Could not start the download engine. Check that the yt-dlp path is valid.</translation>
    </message>
    <message>
        <source>无法启动下载内核。</source>
        <translation type="vanished">Could not start the download engine.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="182"/>
        <source>粘贴视频地址（支持 B站 / YouTube / 抖音 等站点），选好画质后点「开始下载」。</source>
        <translation>Paste a video link (Bilibili / YouTube / Douyin and others), pick a quality, then click Start download.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="185"/>
        <source>https://…（视频页面地址）</source>
        <translation>https://… (video page URL)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="189"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="216"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="241"/>
        <source>浏览…</source>
        <translation>Browse…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="190"/>
        <source>打开保存目录</source>
        <translation>Open save folder</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="193"/>
        <source>保存到</source>
        <translation>Save to</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="199"/>
        <source>最高画质（自动合并为 mp4）</source>
        <translation>Best quality (merged into mp4)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="200"/>
        <source>1080p 及以下</source>
        <translation>1080p or lower</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="201"/>
        <source>720p 及以下</source>
        <translation>720p or lower</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="202"/>
        <source>480p 及以下</source>
        <translation>480p or lower</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="203"/>
        <source>仅音频（mp3）</source>
        <translation>Audio only (mp3)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="206"/>
        <source>画质</source>
        <translation>Quality</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="214"/>
        <source>可选：浏览器导出的 cookies.txt（B站 会员内容等需要）</source>
        <translation>Optional: cookies.txt exported from your browser (needed for members-only content on Bilibili)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="219"/>
        <source>Cookie 文件</source>
        <translation>Cookie file</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="223"/>
        <source>视频地址</source>
        <translation>Video URL</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="230"/>
        <source>下载内核（yt-dlp / ffmpeg）</source>
        <translation>Download engines (yt-dlp / ffmpeg)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="237"/>
        <source>下载 / 更新 yt-dlp</source>
        <translation>Download / update yt-dlp</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="238"/>
        <source>下载 ffmpeg</source>
        <translation>Download ffmpeg</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="239"/>
        <source>手动指定 yt-dlp…</source>
        <translation>Choose yt-dlp manually…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="240"/>
        <source>手动指定 ffmpeg…</source>
        <translation>Choose ffmpeg manually…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="242"/>
        <source>清除</source>
        <translation>Clear</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="248"/>
        <source>可选：多个工具共用的内核目录，例如 D:\Tools\runtime</source>
        <translation>Optional: an engine directory shared by several tools, e.g. D:\Tools\runtime</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="262"/>
        <source>共享内核目录</source>
        <translation>Shared engine directory</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="271"/>
        <source>开始下载</source>
        <translation>Start download</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="272"/>
        <source>取消</source>
        <translation>Cancel</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="279"/>
        <source>就绪。</source>
        <translation>Ready.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="295"/>
        <source>下载日志会显示在这里</source>
        <translation>The download log shows up here</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="298"/>
        <source>日志</source>
        <translation>Log</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="303"/>
        <source>选择保存目录</source>
        <translation>Choose save folder</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="332"/>
        <source>共享内核目录已保存：%1</source>
        <translation>Shared engine directory saved: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="334"/>
        <source>（已清空）</source>
        <translation>(cleared)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="340"/>
        <source>选择共享内核目录</source>
        <translation>Choose the shared engine directory</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="382"/>
        <source>未找到 —— 点下面的按钮下载，或手动指定路径，也可填上面的共享内核目录</source>
        <translation>Not found -- use the buttons below to download it, pick a path manually, or fill in the shared engine directory above</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="385"/>
        <source>未找到 —— 高画质合并与「仅音频」将不可用；可点上面的按钮下载，或手动指定路径 / 填共享内核目录</source>
        <translation>Not found -- high-quality merging and "audio only" will be unavailable; use the buttons above to download it, pick a path manually, or fill in the shared engine directory</translation>
    </message>
    <message>
        <source>ffmpeg</source>
        <translation type="vanished">ffmpeg</translation>
    </message>
    <message>
        <source>yt-dlp</source>
        <translation type="vanished">yt-dlp</translation>
    </message>
    <message>
        <source>未找到 —— 点下面的按钮下载，或手动指定路径</source>
        <translation type="vanished">Not found — use the button below to download it, or point to a path manually</translation>
    </message>
    <message>
        <source>未找到 —— 高画质合并与「仅音频」将不可用</source>
        <translation type="vanished">Not found — high-quality merging and “audio only” will be unavailable</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="389"/>
        <source>yt-dlp：%1
ffmpeg：%2</source>
        <translation>yt-dlp: %1
ffmpeg: %2</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="419"/>
        <source>无法创建目录：%1</source>
        <translation>Could not create the folder: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="420"/>
        <source>无法创建内核目录：%1</source>
        <translation>Could not create the engine folder: %1</translation>
    </message>
    <message>
        <source>开始下载 %1 …</source>
        <translation type="vanished">Starting download of %1 …</translation>
    </message>
    <message>
        <source>正在下载 %1 …</source>
        <translation type="vanished">Downloading %1 …</translation>
    </message>
    <message>
        <source>无法写入临时文件：%1</source>
        <translation type="vanished">Could not write the temporary file: %1</translation>
    </message>
    <message>
        <source>%1 下载中断，正在重试（第 %2 次）…</source>
        <translation type="vanished">Download of %1 interrupted, retrying (attempt %2)…</translation>
    </message>
    <message>
        <source>正在下载 %1 … %2%（%3 / %4 MB）</source>
        <translation type="vanished">Downloading %1 … %2% (%3 / %4 MB)</translation>
    </message>
    <message>
        <source>网络请求已取消</source>
        <translation type="vanished">Network request cancelled</translation>
    </message>
    <message>
        <source>服务器未支持断点续传，重新下载整个文件。</source>
        <translation type="vanished">The server does not support resuming, so the whole file is downloaded again.</translation>
    </message>
    <message>
        <source>第 %1 次下载中断（%2），重试中…</source>
        <translation type="vanished">Attempt %1 interrupted (%2), retrying…</translation>
    </message>
    <message>
        <source>下载失败（已尝试 %1 次）：%2</source>
        <translation type="vanished">Download failed after %1 attempt(s): %2</translation>
    </message>
    <message>
        <source>%1 下载失败。</source>
        <translation type="vanished">Failed to download %1.</translation>
    </message>
    <message>
        <source>保存失败：%1</source>
        <translation type="vanished">Could not save: %1</translation>
    </message>
    <message>
        <source>%1 保存失败。</source>
        <translation type="vanished">Failed to save %1.</translation>
    </message>
    <message>
        <source>yt-dlp 已就绪：%1</source>
        <translation type="vanished">yt-dlp ready: %1</translation>
    </message>
    <message>
        <source>yt-dlp 已就绪。</source>
        <translation type="vanished">yt-dlp is ready.</translation>
    </message>
    <message>
        <source>正在解压 ffmpeg …</source>
        <translation type="vanished">Extracting ffmpeg …</translation>
    </message>
    <message>
        <source>解压失败：%1</source>
        <translation type="vanished">Extraction failed: %1</translation>
    </message>
    <message>
        <source>未知错误</source>
        <translation type="vanished">unknown error</translation>
    </message>
    <message>
        <source>ffmpeg 安装失败：解压出错。</source>
        <translation type="vanished">ffmpeg install failed: extraction error.</translation>
    </message>
    <message>
        <source>解压后没找到 ffmpeg.exe。</source>
        <translation type="vanished">ffmpeg.exe was not found after extraction.</translation>
    </message>
    <message>
        <source>ffmpeg 安装失败：压缩包里没有 ffmpeg.exe。</source>
        <translation type="vanished">ffmpeg install failed: the archive contains no ffmpeg.exe.</translation>
    </message>
    <message>
        <source>ffmpeg 已就绪：%1</source>
        <translation type="vanished">ffmpeg ready: %1</translation>
    </message>
    <message>
        <source>ffmpeg 已就绪。</source>
        <translation type="vanished">ffmpeg is ready.</translation>
    </message>
    <message>
        <source>复制 ffmpeg 失败：%1</source>
        <translation type="vanished">Could not copy ffmpeg: %1</translation>
    </message>
    <message>
        <source>ffmpeg 安装失败：无法写入目标目录。</source>
        <translation type="vanished">ffmpeg install failed: cannot write to the target folder.</translation>
    </message>
    <message>
        <source>无法调用 PowerShell 解压，请手动指定 ffmpeg 路径。</source>
        <translation type="vanished">Could not run PowerShell to extract the archive; choose the ffmpeg path manually.</translation>
    </message>
    <message>
        <source>ffmpeg 安装失败：无法解压。</source>
        <translation type="vanished">ffmpeg install failed: cannot extract the archive.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="458"/>
        <source>选择 yt-dlp 可执行文件</source>
        <translation>Choose the yt-dlp executable</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="459"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="471"/>
        <source>可执行文件 (*.exe);;所有文件 (*)</source>
        <translation>Executables (*.exe);;All files (*)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="464"/>
        <source>已指定 yt-dlp：%1</source>
        <translation>yt-dlp set to %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="470"/>
        <source>选择 ffmpeg 可执行文件</source>
        <translation>Choose the ffmpeg executable</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="476"/>
        <source>已指定 ffmpeg：%1</source>
        <translation>ffmpeg set to %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="482"/>
        <source>选择 cookies.txt</source>
        <translation>Choose cookies.txt</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="483"/>
        <source>Cookie 文件 (*.txt);;所有文件 (*)</source>
        <translation>Cookie files (*.txt);;All files (*)</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="488"/>
        <source>已指定 cookies 文件：%1</source>
        <translation>Cookies file set to %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="497"/>
        <source>缺少地址</source>
        <translation>Missing URL</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="497"/>
        <source>请先粘贴视频地址。</source>
        <translation>Paste a video URL first.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="508"/>
        <source>缺少下载内核</source>
        <translation>Missing download engine</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="509"/>
        <source>还没有可用的 yt-dlp。请点「下载 / 更新 yt-dlp」，或手动指定一个 yt-dlp.exe 路径。</source>
        <translation>There is no usable yt-dlp yet. Click “Download / update yt-dlp”, or point to a yt-dlp.exe manually.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="516"/>
        <source>保存目录无效</source>
        <translation>Invalid save folder</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="516"/>
        <source>请选择一个可写入的保存目录。</source>
        <translation>Choose a save folder that can be written to.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="525"/>
        <source>缺少 ffmpeg</source>
        <translation>ffmpeg missing</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="526"/>
        <source>「仅音频」需要用 ffmpeg 转成 mp3。请先点「下载 ffmpeg」，或改选其它画质。</source>
        <translation>“Audio only” needs ffmpeg to produce an mp3. Click “Download ffmpeg” first, or pick another quality.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="533"/>
        <source>已从粘贴内容中识别出地址：%1</source>
        <translation>Recognised a URL in the pasted text: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="572"/>
        <source>视频标题无法用作文件名，已改用默认命名。</source>
        <translation>The video title cannot be used as a file name; falling back to the default name.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="588"/>
        <source>cookies 文件读不出来，本次下载不使用它：%1</source>
        <translation>The cookies file could not be read, so this download will not use it: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="593"/>
        <source>cookies 文件格式不合规，已自动修正 %1 行、丢弃 %2 行畸形记录。</source>
        <translation>The cookies file was malformed: fixed %1 line(s) and discarded %2 bad row(s).</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="611"/>
        <source>正在解析视频信息…</source>
        <translation>Reading video info…</translation>
    </message>
    <message>
        <source>执行：%1 %2</source>
        <translation type="vanished">Running: %1 %2</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="605"/>
        <source>提示：未检测到 ffmpeg，已降级为单文件下载，清晰度可能受限。</source>
        <translation>Note: ffmpeg was not found, so this falls back to a single-file download and quality may be limited.</translation>
    </message>
    <message>
        <source>抖音要靠浏览器渲染页面才能取到播放地址，但这台机器上没找到 Edge 或 Chrome。</source>
        <translation type="vanished">Douyin needs a browser to render the page before the play URL appears, but neither Edge nor Chrome was found on this machine.</translation>
    </message>
    <message>
        <source>未找到可用的浏览器。</source>
        <translation type="vanished">No usable browser was found.</translation>
    </message>
    <message>
        <source>抖音的播放地址要等页面脚本跑完才出现，正在用浏览器渲染…</source>
        <translation type="vanished">Douyin’s play URL only appears after the page scripts run; rendering it in a browser…</translation>
    </message>
    <message>
        <source>渲染器：%1</source>
        <translation type="vanished">Renderer: %1</translation>
    </message>
    <message>
        <source>正在渲染抖音页面…</source>
        <translation type="vanished">Rendering the Douyin page…</translation>
    </message>
    <message>
        <source>浏览器启动失败：%1</source>
        <translation type="vanished">Could not launch the browser: %1</translation>
    </message>
    <message>
        <source>无法启动浏览器。</source>
        <translation type="vanished">Could not launch the browser.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="630"/>
        <source>已取消。</source>
        <translation>Cancelled.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="603"/>
        <source>执行：%1</source>
        <translation>Running: %1</translation>
    </message>
    <message>
        <source>无法启动浏览器：%1</source>
        <translation type="vanished">Could not start the browser: %1</translation>
    </message>
    <message>
        <source>渲染超时：抖音页面没能在 60 秒内就绪。</source>
        <translation type="vanished">Render timed out: the Douyin page did not become ready within 60 seconds.</translation>
    </message>
    <message>
        <source>抖音页面渲染超时。</source>
        <translation type="vanished">Timed out rendering the Douyin page.</translation>
    </message>
    <message>
        <source>浏览器渲染失败（退出码 %1）。</source>
        <translation type="vanished">Browser rendering failed (exit code %1).</translation>
    </message>
    <message>
        <source>抖音页面渲染失败。</source>
        <translation type="vanished">Failed to render the Douyin page.</translation>
    </message>
    <message>
        <source>页面渲染完了，但里面没有 video_id —— 多半是抖音又改版了。</source>
        <translation type="vanished">The page rendered but contains no video_id — Douyin most likely changed its site again.</translation>
    </message>
    <message>
        <source>没能从抖音页面里取到播放地址。</source>
        <translation type="vanished">Could not extract the play URL from the Douyin page.</translation>
    </message>
    <message>
        <source>已取到 video_id：%1</source>
        <translation type="vanished">Got video_id: %1</translation>
    </message>
    <message>
        <source>标题：%1</source>
        <translation type="vanished">Title: %1</translation>
    </message>
    <message>
        <source>播放地址接口：%1</source>
        <translation type="vanished">Play URL endpoint: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="654"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="655"/>
        <source>已取消内核下载。</source>
        <translation>Engine download cancelled.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="663"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="664"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="673"/>
        <source>正在取消…</source>
        <translation>Cancelling…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="672"/>
        <source>正在取消…（已下载的临时文件可能残留在保存目录）</source>
        <translation>Cancelling… (partial files may be left behind in the save folder)</translation>
    </message>
    <message>
        <source>%1，剩余 %2</source>
        <translation type="vanished">%1, %2 remaining</translation>
    </message>
    <message>
        <source>正在下载… %1%</source>
        <translation type="vanished">Downloading… %1%</translation>
    </message>
    <message>
        <source>正在下载… %1%（%2）</source>
        <translation type="vanished">Downloading… %1% (%2)</translation>
    </message>
    <message>
        <source>正在下载…</source>
        <translation type="vanished">Downloading…</translation>
    </message>
    <message>
        <source>正在合并音视频…</source>
        <translation type="vanished">Merging audio and video…</translation>
    </message>
    <message>
        <source>正在提取音频…</source>
        <translation type="vanished">Extracting audio…</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="628"/>
        <source>下载已取消或进程异常退出。</source>
        <translation>The download was cancelled, or the process exited unexpectedly.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="634"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="635"/>
        <source>下载完成。</source>
        <translation>Download complete.</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="637"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="639"/>
        <source>下载完成：%1</source>
        <translation>Download complete: %1</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="642"/>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="644"/>
        <source>下载失败（退出码 %1）。</source>
        <translation>Download failed (exit code %1).</translation>
    </message>
</context>
<context>
    <name>VideoDlPlugin</name>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="715"/>
        <source>视频下载</source>
        <translation>Video Downloader</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="716"/>
        <source>媒体工具</source>
        <translation>Media tools</translation>
    </message>
    <message>
        <location filename="../plugins/videodl/VideoDlPlugin.cpp" line="719"/>
        <source>粘贴 B站 / YouTube / 抖音 等视频地址，按画质下载到本地（内核为 yt-dlp）。</source>
        <translation>Paste a Bilibili / YouTube / Douyin link and download it locally at the quality you pick (powered by yt-dlp).</translation>
    </message>
</context>
</TS>
