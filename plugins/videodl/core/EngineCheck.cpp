#include "core/EngineCheck.h"

// 数值依据（偏差 9.12）：下限取「远低于现实、又远高于错误页」的值。
// yt-dlp.exe 近年始终在 14 MB 以上并持续变大；ffmpeg 的 gpl 完整构建 zip 在
// 80 MB 量级。8 MB / 32 MB 留足了上涨余量，同时把「只收到几 KB 的
// HTML 错误页」和「下载到一半断掉的大半截」都拦在门外。

namespace videodl {

namespace {

/// yt-dlp.exe 的可信大小下限（字节）。
constexpr qint64 kMinYtDlpBytes = 8 * 1024 * 1024;

/// ffmpeg 压缩包的可信大小下限（字节）。
constexpr qint64 kMinFfmpegZipBytes = 32 * 1024 * 1024;

/// PE 可执行文件的魔数（"MZ"，DOS 头固定开头）。
constexpr char kPeMagic[] = {'M', 'Z'};

/// Zip 存档的魔数（本地文件头 "PK\x03\x04"）。
constexpr char kZipMagic[] = {'P', 'K', '\x03', '\x04'};

EngineFileProblem check(qint64 size, const QByteArray &head, qint64 minBytes, const char *magic,
                        int magicLen)
{
    if (head.size() < magicLen || qstrncmp(head.constData(), magic, magicLen) != 0) {
        return EngineFileProblem::BadHeader;
    }
    if (size < minBytes) {
        return EngineFileProblem::TooSmall;
    }
    return EngineFileProblem::None;
}

} // namespace

EngineFileProblem checkYtDlpBinary(qint64 size, const QByteArray &head)
{
    return check(size, head, kMinYtDlpBytes, kPeMagic, 2);
}

EngineFileProblem checkFfmpegZip(qint64 size, const QByteArray &head)
{
    return check(size, head, kMinFfmpegZipBytes, kZipMagic, 4);
}

} // namespace videodl
