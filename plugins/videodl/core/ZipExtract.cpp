#include "ZipExtract.h"

// 第三方：miniz（单文件 zlib 替代，MIT）。来源与版本钉在 videodl/CMakeLists.txt
// 的 miniz 库定义处；本 TU 是它在产品代码里的唯一消费者。
#include "miniz.h"

#include <QFile>
#include <cstdio>

namespace videodl {
namespace {

constexpr mz_uint32 kZipFlags = 0;
constexpr qsizetype kCopyChunk = 256 * 1024;

/// 条目名里最后一个 '/' 之后的部分。zip 条目分隔符按规范就是 '/'。
QString entryBaseName(const QString &entry)
{
    const qsizetype slash = entry.lastIndexOf(QLatin1Char('/'));
    return slash < 0 ? entry : entry.mid(slash + 1);
}

/// 条目名里最后一段目录名（条目在根下时为空串）。
QString entryParentDirName(const QString &entry)
{
    const qsizetype slash = entry.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) {
        return QString();
    }
    const qsizetype prev = entry.lastIndexOf(QLatin1Char('/'), slash - 1);
    return prev < 0 ? entry.left(slash) : entry.mid(prev + 1, slash - prev - 1);
}

/// 在压缩包条目里挑 ffmpeg.exe：bin/ 目录下的同名条目优先，其余取第一个。
/// 与被它替代的 QDirIterator 版本行为一致 —— 官方包 bin/ 下才是真的主程序。
/// 返回条目下标；找不到返回 -1。
qsizetype findFfmpegEntry(mz_zip_archive *zip)
{
    const mz_uint count = mz_zip_reader_get_num_files(zip);
    qsizetype fallback = -1;
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(zip, i, &stat)) {
            continue;
        }
        const QString entry = QString::fromUtf8(stat.m_filename);
        if (entryBaseName(entry).compare(QStringLiteral("ffmpeg.exe"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        if (entryParentDirName(entry).compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
            return static_cast<qsizetype>(i);
        }
        if (fallback < 0) {
            fallback = static_cast<qsizetype>(i);
        }
    }
    return fallback;
}

/// 逐块把条目解出来写进 out。出错时给出的状态要由调用方负责删半截文件。
ZipExtractStatus extractEntry(mz_zip_archive *zip, mz_uint index, QFile &out)
{
    auto *iter = mz_zip_reader_extract_iter_new(zip, index, kZipFlags);
    if (!iter) {
        return ZipExtractStatus::CantOpenZip;
    }

    ZipExtractStatus status = ZipExtractStatus::Ok;
    QByteArray chunk(kCopyChunk, Qt::Uninitialized);
    for (;;) {
        const size_t got =
            mz_zip_reader_extract_iter_read(iter, chunk.data(), static_cast<size_t>(chunk.size()));
        if (got == 0) {
            break;
        }
        if (out.write(chunk.constData(), static_cast<qint64>(got)) != static_cast<qint64>(got)) {
            status = ZipExtractStatus::WriteFailed;
            break;
        }
    }
    // iter_free 返回假说明校验没过（CRC 不符）：文件是坏的，不能当成功收场。
    if (!mz_zip_reader_extract_iter_free(iter) && status == ZipExtractStatus::Ok) {
        status = ZipExtractStatus::CantOpenZip;
    }
    return status;
}

} // namespace

ZipExtractStatus extractFfmpegMainExecutable(const QString &zipPath, const QString &targetPath)
{
    // miniz 自带的 mz_zip_reader_init_file 走 fopen，MSVC 按 ANSI 代码页解释路径
    // —— 用户名带中文时临时目录直接打不开。所以 zip 侧用 _wfopen_s 拿 FILE* 喂给
    // mz_zip_reader_init_cfile；目标侧用 QFile。两头的 Unicode 路径都安全。
    FILE *zipFile = nullptr;
    if (_wfopen_s(&zipFile, reinterpret_cast<const wchar_t *>(zipPath.utf16()), L"rb") != 0
        || !zipFile) {
        return ZipExtractStatus::CantOpenZip;
    }

    mz_zip_archive zip = {};
    const qint64 zipSize = QFile(zipPath).size();
    if (!mz_zip_reader_init_cfile(&zip, zipFile, static_cast<mz_uint64>(zipSize), kZipFlags)) {
        fclose(zipFile);
        return ZipExtractStatus::CantOpenZip;
    }

    ZipExtractStatus status = ZipExtractStatus::Ok;
    const qsizetype entry = findFfmpegEntry(&zip);
    if (entry < 0) {
        status = ZipExtractStatus::NoExecutable;
    } else {
        // 旧文件先删再写：它是上一次安装的残留，直接覆写遇「文件被占用」会得到
        // 一个写了一半的 ffmpeg.exe。删不掉（多半是它还在运行）也会在下面
        // 打开时失败，走 WriteFailed。
        QFile::remove(targetPath);
        QFile out(targetPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            status = ZipExtractStatus::WriteFailed;
        } else {
            status = extractEntry(&zip, static_cast<mz_uint>(entry), out);
            out.close();
        }
        if (status != ZipExtractStatus::Ok) {
            QFile::remove(targetPath); // 半截文件不留
        }
    }

    mz_zip_reader_end(&zip);
    fclose(zipFile);
    return status;
}

} // namespace videodl
