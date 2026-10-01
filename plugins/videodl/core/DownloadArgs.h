#pragma once

#include <QString>
#include <QStringList>

// 视频下载插件里「怎么把一次下载翻译成 yt-dlp 的命令行参数」的那部分纯逻辑。
//
// 这些分支（画质档位、有没有 ffmpeg、要不要 referer、用不用 cookies、标题能不能
// 当文件名）以前全在页面的 launchDownload() 里，改一个档位只能靠真下一次载去试。
// 抽出来之后「给一组输入，得到什么参数」可以直接断言（见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 一次下载的全部输入。
struct DownloadSpec
{
    QString url;
    /// 页面标题。留空则用默认命名模板（`%(title)s [%(id)s].%(ext)s`）；
    /// 非空时会被当成文件名用，**调用方必须先消毒**（见 sanitizeFileName）。
    QString title;
    QString outputDir;
    /// 画质下拉框下标：0 最高、1–3 为 1080/720/480 及以下、4 仅音频。
    int quality = 0;
    bool audioOnly = false;
    /// 目标 CDN 会检查来源（抖音），需要补 Referer。
    bool needsReferer = false;
    /// 有没有可用的 ffmpeg：没有就没法合并分轨，只能退回单文件（清晰度会掉）。
    bool hasFfmpeg = false;
    /// 已规范化的 cookies 副本路径。留空表示本次不使用 cookies。
    QString cookiesPath;
    /// ffmpeg 所在目录（yt-dlp 自己会去里面找），仅 hasFfmpeg 时传。
    QString ffmpegDir;
};

/// 构造 yt-dlp 的参数（不含程序名）。
///
/// 顺序与取值都是 yt-dlp 的约定，注释里写明了每一处「为什么」——
/// 尤其是没有 ffmpeg 时退回单文件那条，它决定了清晰度上限。
QStringList buildYtDlpArgs(const DownloadSpec &spec);

} // namespace videodl
