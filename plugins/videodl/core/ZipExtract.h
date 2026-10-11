#pragma once

#include <QString>

namespace videodl {

/// 从 ffmpeg zip 里提取主程序的结果。每个失败态对应一句用户能看懂的解释，
/// 由调用方（EngineFetcher）翻译成界面文案 —— core 不持有 tr() 文案。
enum class ZipExtractStatus {
    Ok,           ///< 提取成功，目标文件已就位
    CantOpenZip,  ///< 打不开或读不懂这个 zip（下载损坏 / 被安全软件改写 / CRC 不过）
    NoExecutable, ///< 压缩包里找不到 ffmpeg.exe
    WriteFailed   ///< 目标路径打不开或写不进去（权限 / 磁盘满 / 旧文件被占用）
};

/// 从 zipPath 的压缩包里找出 ffmpeg.exe 并解压到 targetPath。
///
/// 多个同名条目时优先 bin/ 目录下的那个（官方包的条目都挂在
/// <版本>-full_build/bin/ 下，doc/ 等目录若也出现同名以 bin/ 为准）。
///
/// 同步且阻塞：ffmpeg 的 zip 近百 MB，整个提取约一两秒，调用方必须放到
/// 工作线程（EngineFetcher 用 QtConcurrent::run），不得在主线程直呼。
/// 本函数不碰界面、不碰网络、不起子进程 —— 受限环境拦子进程也不影响它。
[[nodiscard]] ZipExtractStatus extractFfmpegMainExecutable(const QString &zipPath,
                                                           const QString &targetPath);

} // namespace videodl
