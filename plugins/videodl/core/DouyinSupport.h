#pragma once

#include <QString>

// 视频下载插件里「抖音专线」的那部分纯逻辑。
//
// 抖音不能交给 yt-dlp 直连：它的 web detail 接口要签名，yt-dlp 至今没实现，
// 必然 403。这里只放与子进程、浏览器无关的判定与解析规则，让「什么算抖音
// 地址」「哪个画质档位对应哪个 ratio」「怎么从渲染好的 DOM 里取字段」这些
// 规则可以脱离界面单测（见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 地址是不是抖音站点。大小写不敏感。
bool isDouyinUrl(const QString &url);

/// 画质下拉框下标 → 抖音播放接口的 ratio 参数。
///
/// 抖音只认这几档；「仅音频」也得先取到视频流再由 ffmpeg 抽音轨，所以同样
/// 按最高档拿。下标越界一律回落到最高档，不让界面传进来的脏值变成空串。
QString douyinRatio(int quality);

/// 从浏览器渲染出的 DOM 里取 video_id。取不到返回空串。
///
/// video_id 只有等抖音自己的页面脚本跑完才会出现在 DOM 里，所以这里的输入
/// 必须是渲染后的整份 DOM，不能是原始响应体。
QString parseDouyinVideoId(const QString &dom);

/// 从渲染出的 DOM 里取页面标题，并去掉「 - 抖音」后缀。取不到返回空串。
QString parseDouyinTitle(const QString &dom);

} // namespace videodl
