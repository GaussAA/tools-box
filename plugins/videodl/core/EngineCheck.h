#pragma once

#include <QByteArray>

// 视频下载插件里「内核下载落地后的最小完整性校验」纯逻辑。
//
// 背景（偏差 9.12）：yt-dlp / ffmpeg 从 GitHub 的 releases/latest 下载，没有版本
// 可钉、也没有哈希可对 —— 钉旧版本会让下载功能跟着站点对抗一起静默失效，手维护
// 哈希表的成本与漂移风险都高。这里退而求其次，拦「最常见的损坏」：
//   1. 下载到的是 HTML 错误页 / 反爬页（文件头不是期望的魔数）；
//   2. 严重截断的文件（大小远低于现实值）。
// 这不是安全边界，是可用性闸门；若上游哪天提供稳定的校验和清单，再升级为哈希校验。
//
// 与界面无关，抽进 core/ 是为了能直接单测（见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 校验结论。
enum class EngineFileProblem {
    None,      ///< 通过
    BadHeader, ///< 文件头不是期望的魔数（不像 PE / 不像 Zip）
    TooSmall   ///< 大小低于可信下限
};

/// 校验下载落地的 yt-dlp.exe：PE 头（MZ）+ 大小下限。
///
/// size 是文件总大小（字节），head 是文件开头的字节（至少 2 字节，不足按有的算）。
EngineFileProblem checkYtDlpBinary(qint64 size, const QByteArray &head);

/// 校验下载落地的 ffmpeg 压缩包：Zip 头（PK\x03\x04）+ 大小下限。
///
/// 参数约定同 checkYtDlpBinary。
EngineFileProblem checkFfmpegZip(qint64 size, const QByteArray &head);

} // namespace videodl
