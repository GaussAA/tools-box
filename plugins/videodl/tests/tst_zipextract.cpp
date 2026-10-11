// ZipExtract 的用例：从 ffmpeg zip 里挑对条目、各失败态报对原因。
//
// 测试用 miniz 的 **writer** 现场造 zip —— 既不依赖任何二进制 fixture，也顺带
// 证明 miniz 在构建里真的能用。全程进程内，不碰网络也不碰子进程。

#include "core/ZipExtract.h"
#include "miniz.h" // videodl_core PUBLIC 链接 miniz，include 目录随之传入

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
#include <cstdio>

class TestZipExtract : public QObject
{
    Q_OBJECT

private slots:
    void extractsTheBinCopyWhenEntriesShareTheName();
    void picksNonBinEntryWhenNoBinDirectoryExists();
    void reportsNoExecutableWhenArchiveLacksOne();
    void reportsCantOpenZipForGarbageFile();
    void reportsCantOpenZipForMissingFile();
    void reportsWriteFailedWhenTargetCantBeOpened();
    void overwritesLeftoverTargetFromPreviousInstall();
};

namespace {

struct ZipSpec
{
    QString entry;
    QByteArray content;
};

/// 用 miniz writer 造一个真 zip。路径经 _wfopen_s 打开，与生产代码同一套
/// Unicode 安全做法 —— 这里若改用按 ANSI 代码页解释路径的 fopen，测试机
/// 用户名带中文时就会假红。
bool writeZip(const QString &path, const QList<ZipSpec> &entries)
{
    FILE *file = nullptr;
    if (_wfopen_s(&file, reinterpret_cast<const wchar_t *>(path.utf16()), L"wb+") != 0 || !file) {
        return false;
    }
    mz_zip_archive zip = {};
    if (!mz_zip_writer_init_cfile(&zip, file, 0)) {
        fclose(file);
        return false;
    }
    bool ok = true;
    for (const ZipSpec &one : entries) {
        if (!mz_zip_writer_add_mem(&zip, one.entry.toUtf8().constData(), one.content.constData(),
                                   static_cast<size_t>(one.content.size()), MZ_DEFAULT_LEVEL)) {
            ok = false;
            break;
        }
    }
    if (ok && !mz_zip_writer_finalize_archive(&zip)) {
        ok = false;
    }
    mz_zip_writer_end(&zip);
    fclose(file);
    return ok;
}

QByteArray readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

} // namespace

// 官方包里 bin/ 下的才是真主程序；两个同名条目并存时必须挑 bin/ 那份，
// 拿成 doc/ 下的会得到一个「看起来装好了」的假 ffmpeg。
void TestZipExtract::extractsTheBinCopyWhenEntriesShareTheName()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    const QString target = dir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(writeZip(
        zipPath,
        {{QStringLiteral("ffmpeg-7.1-full_build/bin/ffmpeg.exe"), QByteArrayLiteral("BIN_COPY")},
         {QStringLiteral("ffmpeg-7.1-full_build/bin/ffprobe.exe"), QByteArrayLiteral("PROBE")},
         {QStringLiteral("ffmpeg-7.1-full_build/doc/ffmpeg.exe"), QByteArrayLiteral("DOC_COPY")}}));

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, target), videodl::ZipExtractStatus::Ok);
    QCOMPARE(readAll(target), QByteArrayLiteral("BIN_COPY"));
}

// 没有 bin/ 目录（条目在根下或别的目录）时退而取第一个同名条目，
// 而不是报「没找到」—— 找得到比挑得精更重要，这条钉的是回退行为。
void TestZipExtract::picksNonBinEntryWhenNoBinDirectoryExists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    const QString target = dir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(writeZip(zipPath,
                     {{QStringLiteral("ffmpeg-master-latest-win64-gpl/ffmpeg.exe"),
                       QByteArrayLiteral("ROOT_COPY")}}));

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, target), videodl::ZipExtractStatus::Ok);
    QCOMPARE(readAll(target), QByteArrayLiteral("ROOT_COPY"));
}

void TestZipExtract::reportsNoExecutableWhenArchiveLacksOne()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    const QString target = dir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(writeZip(zipPath,
                     {{QStringLiteral("pkg/bin/ffprobe.exe"), QByteArrayLiteral("PROBE")},
                      {QStringLiteral("pkg/readme.txt"), QByteArrayLiteral("hi")}}));

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, target),
             videodl::ZipExtractStatus::NoExecutable);
    QVERIFY2(!QFile::exists(target), "失败时不得留下半截文件");
}

// 把 HTML 错误页当 zip 拿来解（上游被墙 / 反爬时下载环节的常见产物）
// 必须报「打不开」，而不是闷头写出一个空文件。
void TestZipExtract::reportsCantOpenZipForGarbageFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    QFile garbage(zipPath);
    QVERIFY(garbage.open(QIODevice::WriteOnly));
    garbage.write(QByteArrayLiteral("<!DOCTYPE html>"));
    garbage.close();

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, dir.path() + QStringLiteral("/x.exe")),
             videodl::ZipExtractStatus::CantOpenZip);
}

void TestZipExtract::reportsCantOpenZipForMissingFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(videodl::extractFfmpegMainExecutable(dir.path() + QStringLiteral("/nope.zip"),
                                                  dir.path() + QStringLiteral("/x.exe")),
             videodl::ZipExtractStatus::CantOpenZip);
}

// 目标路径打不开要报 WriteFailed，且不留下半截文件。
// （最初想用「QFile 持锁」模拟目标被占用，但 Qt 打开文件默认带读写共享，
// 第二个写句柄照样成功 —— 模拟不出 Windows 的独占。改走「父目录不存在」，
// 触发的是同一个失败分支：QFile::open 失败。真机上的「旧 ffmpeg.exe 还在
// 运行」由系统镜像锁兜底，同样是 open/remove 失败。）
void TestZipExtract::reportsWriteFailedWhenTargetCantBeOpened()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    const QString target = dir.path() + QStringLiteral("/no_such_dir/ffmpeg.exe");
    QVERIFY(
        writeZip(zipPath, {{QStringLiteral("pkg/bin/ffmpeg.exe"), QByteArrayLiteral("BIN_COPY")}}));

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, target),
             videodl::ZipExtractStatus::WriteFailed);
    QVERIFY2(!QFile::exists(target), "失败时不得留下半截文件");
}

// 上一次安装的残留必须被替换 —— 覆写不是追加，否则新旧内容拼成一个坏文件。
void TestZipExtract::overwritesLeftoverTargetFromPreviousInstall()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString zipPath = dir.path() + QStringLiteral("/ffmpeg.zip");
    const QString target = dir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(writeZip(zipPath, {{QStringLiteral("pkg/bin/ffmpeg.exe"), QByteArrayLiteral("NEW")}}));
    {
        QFile leftover(target);
        QVERIFY(leftover.open(QIODevice::WriteOnly));
        leftover.write(QByteArrayLiteral("OLD_LEFTOVER_WAY_LONGER_THAN_NEW"));
    }

    QCOMPARE(videodl::extractFfmpegMainExecutable(zipPath, target), videodl::ZipExtractStatus::Ok);
    QCOMPARE(readAll(target), QByteArrayLiteral("NEW"));
}

QTEST_APPLESS_MAIN(TestZipExtract)

#include "tst_zipextract.moc"
