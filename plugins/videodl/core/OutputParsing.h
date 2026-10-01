#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// 视频下载插件里「解析外部输出」的那部分纯逻辑：程序输出解码、日志清洗、
// 地址提取、进度与阶段识别、最终产物路径提取。
//
// 放在 core/ 而不是 VideoDlPlugin.cpp 里，是为了让这些规则能脱离界面和真实
// 子进程单测（见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess 等会引入
// 环境依赖的类型 —— 输入输出必须都是可以直接构造的数据。

namespace videodl {

/// 程序输出可能是 UTF-8，也可能是本地代码页（Windows 控制台默认）。
/// 先用 UTF-8 解，出现替换字符就退回本地编码，避免中文标题变乱码。
QString decodeOutput(const QByteArray &bytes);

/// yt-dlp 有时会输出终端配色转义序列，日志里显示出来很脏，直接剥掉。
QString stripAnsi(const QString &text);

/// 从用户粘贴的内容里提取 http(s) 地址。
///
/// 从 App 里点「复制链接」拿到的常常是一整段分享文案：表情、话题标签、口令
/// 和说明文字全都混在一起，真正的地址只是其中一小段。字符集按 RFC 3986 的
/// 合法字符来，中文和反引号之类不在此列，于是「…/mevuYgCRF1g/ 复制此链接」
/// 这种情况会在中文处自然截断。抽不到地址时原样返回输入。
///
/// 调用方负责先做 trim —— 那是界面输入的事，不属于解析规则。
QString extractUrl(const QString &input);

/// 把外部给的文本收拾成可以当文件名用的样子。
///
/// 页面标题这类文本来自站点的 DOM，属于**外部输入**，不能直接拼进下载模板：
/// 里面一个 `/` 或 `\` 就会被当成路径分隔符，把文件写到保存目录之外。
/// 处理规则：不可见字符与 Windows 文件名非法字符换成下划线；首尾的空白与点去掉；
/// 撞上设备名（CON / NUL / COM1…）加前缀破开；最后按长度截断。
///
/// 返回空串表示「这个标题救不回来了」，调用方应回落到默认的输出模板，
/// 而不是拿空名字去下载。上限 120 个 UTF-16 码元。
QString sanitizeFileName(const QString &title);

/// 一行输出里解析出的下载进度。
struct ProgressInfo
{
    bool matched = false; ///< 该行是不是 [download] xx% 进度行
    int percent = 0;      ///< 百分比，已取整
    QString speed;        ///< 形如 1.23MiB/s；该行没带则为空
    QString eta;          ///< 形如 00:12；该行没带则为空
};

/// 解析 [download] 进度行。速度与剩余时间是同一个行里的附带信息，一并取出，
/// 免得用户只能盯着一个百分比猜还要等多久。
ProgressInfo parseProgress(const QString &line);

/// 输出行的阶段标记。yt-dlp 在合并、转码这些阶段不吐百分比，界面据此把进度条
/// 切成不确定态，否则会一直停在 100% 看着像卡死。
enum class OutputStage {
    None,
    DownloadStarting, ///< [download] Destination: 刚落到磁盘
    Merging,          ///< [Merger] 合并音视频
    ExtractingAudio,  ///< [ExtractAudio] / [ffmpeg] 提取音频
};

/// 识别阶段标记。**只在 parseProgress 未命中时调用**，与现有输出解析顺序一致。
OutputStage classifyStage(const QString &line);

/// 解析 HTTP `Content-Range` 响应头的起始字节（`bytes 100-200/300` → 100）。
///
/// 断点续传的正确性全靠它：带上 `Range` 之后，服务端**可以**不从我们请求的偏移
/// 开始给（206 也可能给别的区间）。不校验就把收到的字节往旧的 .part 后面拼，
/// 会得到一个长度对得上、内容却是错的文件 —— 那种损坏不会报错，只会等到播放时
/// 才发现。解析不出（头缺失或格式不认识）时返回 -1，由调用方决定怎么办。
qint64 parseContentRangeStart(const QString &headerValue);

/// 把待执行的命令行整理成可以放心显示的样子。
///
/// 日志里那行「执行：…」不是给自己看的：它会被截图、被贴在问题反馈里。
/// cookies 副本的路径是**登录凭据的所在**，原样打出去等于告诉别人凭据在哪；
/// 超长的参数（多是带签名或 id 的 URL）也一律截断。
QString redactCommand(const QString &program, const QStringList &args);

/// 提取最终产物路径。普通下载给的是 [download] Destination，合并/转码后给的是
/// [Merger] Merging formats into，两种都要认，否则界面报不出下载到哪去了。
/// 该行不是产物行时返回空串。
QString parseDestination(const QString &line);

} // namespace videodl
