#pragma once

#include "core/EngineCheck.h"

#include <QString>

// 视频下载插件里「把下载好的 .part 变成可用的内核文件」的那段处置逻辑。
//
// 这段原先写在 EngineFetcher::finish() 里，与 QObject 揉在一起，于是「坏文件
// 会不会真的被删掉」这种契约无法验证 —— 而它一旦写错，后果是坏内核留在磁盘上，
// 下次启动 EngineLocator 会把它当成已装好的内核直接用，症状推迟到「下载莫名
// 失败」才出现，且与真正的原因隔着好几层。
//
// 抽出来之后可以直接用临时目录验证：正常文件就位、坏文件（HTML 错误页 /
// 严重截断）被删并报出问题类型、替换失败时半截也不留。
//
// 与 EngineCheck 的分工：那边只判「这个文件像不像可用的内核」，这边负责
// 「判完之后怎么处置文件」。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 落地处置的结果。
enum class EngineInstallStatus {
    Installed,       ///< 校验通过，.part 已就位为 target
    RejectedBadFile, ///< 校验不过（不是可用的 exe / zip），.part 已删除
    RenameFailed,    ///< 校验通过但替换失败（目标被占用、无权限、目标是目录），.part 已删除
};

/// 一次落地的结果详情。
struct EngineInstallResult
{
    EngineInstallStatus status = EngineInstallStatus::Installed;
    /// 仅在 RejectedBadFile 时有意义：告诉调用方该报「文件头不对」还是「只有 N 字节」。
    EngineFileProblem problem = EngineFileProblem::None;
    /// 校验时的 .part 大小（字节），供日志文案使用。
    qint64 size = 0;
};

/// 落地 yt-dlp.exe：按 PE 规则校验，通过则把 partPath 改名为 targetPath。
///
/// 失败时（无论哪种）都会删掉 partPath —— 不留半截、不留坏文件。
EngineInstallResult installYtDlp(const QString &partPath, const QString &targetPath);

/// 落地 ffmpeg 的压缩包：按 Zip 规则校验，通过则把 partPath 改名为 targetPath。
///
/// 失败时的清理规则同 installYtDlp。
EngineInstallResult installFfmpegZip(const QString &partPath, const QString &targetPath);

} // namespace videodl
