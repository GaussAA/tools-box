#include "core/EngineInstall.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

// 内核落地处置：正常就位、坏文件必删、替换失败不留半截。
//
// 这几条原先埋在 EngineFetcher::finish() 里，只能靠「下次启动时内核坏了」这种
// 间接症状发现；现在用临时目录直接验证文件系统层面的契约 —— 尤其是「坏文件
// 一定会被删」，因为它一旦漏掉，EngineLocator 会把坏内核当成已装好的直接用。
class TestEngineInstall : public QObject
{
    Q_OBJECT

private slots:
    void installsValidYtDlpPart();
    void installsValidFfmpegZipPart();
    void replacesExistingTarget();
    void rejectsHtmlErrorPageAndDeletesIt();
    void rejectsTruncatedBinaryAndDeletesIt();
    void rejectsPeFileWhenZipExpected();
    void missingPartIsRejectedAsBadHeader();
    void renameFailureCleansUpPart();
    void leavesNoPartFileBehindInAnyCase();
};

namespace {

/// 写一个指定内容与大小的文件，返回路径。
QString writeFile(const QString &path, const QByteArray &head, qint64 totalSize)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(head);
    // 用分块补齐，避免测试里真的写几十 MB。
    const QByteArray filler(1024 * 1024, 'x');
    qint64 written = head.size();
    while (written < totalSize) {
        const qint64 chunk = qMin<qint64>(filler.size(), totalSize - written);
        file.write(filler.constData(), chunk);
        written += chunk;
    }
    file.close();
    return path;
}

constexpr qint64 kBigEnough = 9 * 1024 * 1024;  ///< 高于 yt-dlp 的 8 MiB 下限
constexpr qint64 kTooSmall = 1024;              ///< 头对但远低于下限

} // namespace

void TestEngineInstall::installsValidYtDlpPart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString part = writeFile(dir.path() + QStringLiteral("/yt-dlp.exe.part"),
                                   QByteArrayLiteral("MZ"), kBigEnough);
    QVERIFY(!part.isEmpty());
    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");

    const videodl::EngineInstallResult result = videodl::installYtDlp(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::Installed);
    QCOMPARE(result.problem, videodl::EngineFileProblem::None);
    QCOMPARE(result.size, kBigEnough);
    QVERIFY(QFileInfo::exists(target));
    QVERIFY2(!QFileInfo::exists(part), "就位后不该留下 .part");
    // 内容必须是改名过来的那份，不是被清空的。
    QFile installed(target);
    QVERIFY(installed.open(QIODevice::ReadOnly));
    QCOMPARE(installed.read(2), QByteArrayLiteral("MZ"));
}

void TestEngineInstall::installsValidFfmpegZipPart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString part = writeFile(dir.path() + QStringLiteral("/ffmpeg.zip.part"),
                                   QByteArrayLiteral("PK\x03\x04"), 33 * 1024 * 1024);
    QVERIFY(!part.isEmpty());
    const QString target = dir.path() + QStringLiteral("/ffmpeg.zip");

    const videodl::EngineInstallResult result = videodl::installFfmpegZip(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::Installed);
    QVERIFY(QFileInfo::exists(target));
    QVERIFY(!QFileInfo::exists(part));
}

void TestEngineInstall::replacesExistingTarget()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 旧内核已在位（更新是覆盖式）
    const QString target =
        writeFile(dir.path() + QStringLiteral("/yt-dlp.exe"), QByteArrayLiteral("MZ"), kBigEnough);
    const QString part = writeFile(dir.path() + QStringLiteral("/yt-dlp.exe.part"),
                                   QByteArrayLiteral("MZ"), kBigEnough);
    QVERIFY(!part.isEmpty());

    const videodl::EngineInstallResult result = videodl::installYtDlp(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::Installed);
    QVERIFY(QFileInfo::exists(target));
    QVERIFY(!QFileInfo::exists(part));
}

void TestEngineInstall::rejectsHtmlErrorPageAndDeletesIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 大小达标但内容是 404 页面：最典型的「下载成功却是垃圾」
    const QString part = writeFile(dir.path() + QStringLiteral("/yt-dlp.exe.part"),
                                   QByteArrayLiteral("<!DOCTYPE html>"), kBigEnough);
    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");

    const videodl::EngineInstallResult result = videodl::installYtDlp(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::RejectedBadFile);
    QCOMPARE(result.problem, videodl::EngineFileProblem::BadHeader);
    QVERIFY2(!QFileInfo::exists(part), "坏文件必须删掉，否则下次启动会被当成已装好的内核");
    QVERIFY(!QFileInfo::exists(target));
}

void TestEngineInstall::rejectsTruncatedBinaryAndDeletesIt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString part = writeFile(dir.path() + QStringLiteral("/yt-dlp.exe.part"),
                                   QByteArrayLiteral("MZ"), kTooSmall);
    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");

    const videodl::EngineInstallResult result = videodl::installYtDlp(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::RejectedBadFile);
    QCOMPARE(result.problem, videodl::EngineFileProblem::TooSmall);
    QCOMPARE(result.size, kTooSmall);
    QVERIFY(!QFileInfo::exists(part));
    QVERIFY(!QFileInfo::exists(target));
}

void TestEngineInstall::rejectsPeFileWhenZipExpected()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 张冠李戴：把 exe 当成 ffmpeg 的 zip 交上来
    const QString part = writeFile(dir.path() + QStringLiteral("/ffmpeg.zip.part"),
                                   QByteArrayLiteral("MZ"), 33 * 1024 * 1024);
    const QString target = dir.path() + QStringLiteral("/ffmpeg.zip");

    const videodl::EngineInstallResult result = videodl::installFfmpegZip(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::RejectedBadFile);
    QCOMPARE(result.problem, videodl::EngineFileProblem::BadHeader);
    QVERIFY(!QFileInfo::exists(part));
}

void TestEngineInstall::missingPartIsRejectedAsBadHeader()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ghost = dir.path() + QStringLiteral("/never-downloaded.exe.part");

    // .part 根本不存在：读不到文件头，按「文件头不对」处理，且不能崩。
    const videodl::EngineInstallResult result =
        videodl::installYtDlp(ghost, dir.path() + QStringLiteral("/yt-dlp.exe"));

    QCOMPARE(result.status, videodl::EngineInstallStatus::RejectedBadFile);
    QCOMPARE(result.problem, videodl::EngineFileProblem::BadHeader);
}

void TestEngineInstall::renameFailureCleansUpPart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 把目标路径占成一个目录：rename 必然失败（等价于「目标被占用 / 无权限」）
    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");
    QVERIFY(QDir().mkpath(target));
    const QString part = writeFile(dir.path() + QStringLiteral("/yt-dlp.exe.part"),
                                   QByteArrayLiteral("MZ"), kBigEnough);
    QVERIFY(!part.isEmpty());

    const videodl::EngineInstallResult result = videodl::installYtDlp(part, target);

    QCOMPARE(result.status, videodl::EngineInstallStatus::RenameFailed);
    QVERIFY2(!QFileInfo::exists(part), "替换失败时半截也要清掉");
    QVERIFY(QFileInfo(target).isDir()); // 目标仍是原来那个目录，没被破坏
}

void TestEngineInstall::leavesNoPartFileBehindInAnyCase()
{
    // 汇总断言：三条失败路径都不留 .part。这是本模块存在的意义所在。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QStringList parts = {
        writeFile(dir.path() + QStringLiteral("/a.part"), QByteArrayLiteral("MZ"), kBigEnough),
        writeFile(dir.path() + QStringLiteral("/b.part"), QByteArrayLiteral("nope"), kBigEnough),
        writeFile(dir.path() + QStringLiteral("/c.part"), QByteArrayLiteral("MZ"), kTooSmall),
    };
    for (const QString &part : parts) {
        QVERIFY(!part.isEmpty());
    }

    QVERIFY(videodl::installYtDlp(parts.at(0), dir.path() + QStringLiteral("/a.exe")).status
            == videodl::EngineInstallStatus::Installed);
    QVERIFY(videodl::installYtDlp(parts.at(1), dir.path() + QStringLiteral("/b.exe")).status
            == videodl::EngineInstallStatus::RejectedBadFile);
    QVERIFY(videodl::installYtDlp(parts.at(2), dir.path() + QStringLiteral("/c.exe")).status
            == videodl::EngineInstallStatus::RejectedBadFile);

    QVERIFY(!QFileInfo::exists(parts.at(0)));
    QVERIFY(!QFileInfo::exists(parts.at(1)));
    QVERIFY(!QFileInfo::exists(parts.at(2)));
}

QTEST_APPLESS_MAIN(TestEngineInstall)

#include "tst_engineinstall.moc"
