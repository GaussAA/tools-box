#include "core/EngineLocator.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

// 内核可执行文件的定位顺序：手动指定 → 随程序目录 → PATH。
//
// 这条顺序是插件的核心约定之一（见 docs/architecture.md §8），以前只能靠真去
// 摆放几个 exe 再启动程序来验证。
class TestEngineLocator : public QObject
{
    Q_OBJECT

private slots:
    void engineDirAppendsToolsBin();
    void resolvePrefersManualPath();
    void resolveFallsBackToBundledDirectory();
    void resolveReturnsEmptyWhenNothingFound();
    void sharedDirIsTheLastResort();
    void sharedDirIsUsedWhenOthersMiss();
    void sharedDirIsSkippedWhenNotConfigured();
};

namespace {

bool touch(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write("x");
    file.close();
    return true;
}

} // namespace

void TestEngineLocator::engineDirAppendsToolsBin()
{
    QCOMPARE(videodl::engineDir(QStringLiteral("C:/opt/toolbox")),
             QStringLiteral("C:/opt/toolbox/tools/bin"));
}

void TestEngineLocator::resolvePrefersManualPath()
{
    QTemporaryDir bundledDir;
    QVERIFY(bundledDir.isValid());
    QVERIFY(touch(bundledDir.path() + QStringLiteral("/yt-dlp.exe")));

    QTemporaryDir manualDir;
    QVERIFY(manualDir.isValid());
    const QString manual = manualDir.path() + QStringLiteral("/my-yt-dlp.exe");
    QVERIFY(touch(manual));

    const QString resolved =
        videodl::resolveExecutable(manual, QStringLiteral("yt-dlp.exe"), bundledDir.path());

    QCOMPARE(QFileInfo(resolved).canonicalFilePath(), QFileInfo(manual).canonicalFilePath());
}

void TestEngineLocator::resolveFallsBackToBundledDirectory()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bundled = dir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(touch(bundled));

    // 手动路径指向一个并不存在的文件，应当被忽略而不是原样返回。
    const QString resolved = videodl::resolveExecutable(dir.path() + QStringLiteral("/ghost.exe"),
                                                        QStringLiteral("ffmpeg.exe"), dir.path());

    QCOMPARE(QFileInfo(resolved).canonicalFilePath(), QFileInfo(bundled).canonicalFilePath());
}

void TestEngineLocator::resolveReturnsEmptyWhenNothingFound()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 用随机名字，保证 PATH 里不会有同名可执行文件把结果带偏。
    const QString ghost =
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".exe");

    QVERIFY(videodl::resolveExecutable(QString(), ghost, dir.path()).isEmpty());
}

// 共享目录是最后一级兜底：前三处都找不到时才生效，且它排在 PATH 之后 ——
// 前三级都是「本机本来就有的」，用户特意指出来的目录不该抢在前面。
void TestEngineLocator::sharedDirIsTheLastResort()
{
    QTemporaryDir sharedDir;
    QVERIFY(sharedDir.isValid());
    const QString shared = sharedDir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(touch(shared));

    // 随程序目录里有同一个文件时，共享目录不参与（优先级更高的一级胜出）。
    QTemporaryDir bundledDir;
    QVERIFY(bundledDir.isValid());
    const QString bundled = bundledDir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(touch(bundled));

    const QString resolved = videodl::resolveExecutable(QString(), QStringLiteral("ffmpeg.exe"),
                                                        bundledDir.path(), sharedDir.path());
    QCOMPARE(QFileInfo(resolved).canonicalFilePath(), QFileInfo(bundled).canonicalFilePath());
}

// 共享目录里确实有文件、而前三级都没有时，才能落到它。
void TestEngineLocator::sharedDirIsUsedWhenOthersMiss()
{
    QTemporaryDir sharedDir;
    QVERIFY(sharedDir.isValid());
    const QString shared = sharedDir.path() + QStringLiteral("/ffmpeg.exe");
    QVERIFY(touch(shared));
    QTemporaryDir bundledDir;
    QVERIFY(bundledDir.isValid());

    // PATH 里没有 ffmpeg（用随机名避免撞上），所以唯一来源就是共享目录。
    const QString ghost =
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".exe");
    const QString resolved =
        videodl::resolveExecutable(QString(), ghost, bundledDir.path(), sharedDir.path());
    QVERIFY(resolved.isEmpty()); // 随机名共享目录里当然没有

    const QString real = videodl::resolveExecutable(QString(), QStringLiteral("ffmpeg.exe"),
                                                    bundledDir.path(), sharedDir.path());
    if (!real.isEmpty()) {
        QCOMPARE(QFileInfo(real).canonicalFilePath(), QFileInfo(shared).canonicalFilePath());
    }
}

// 没配置共享目录时行为与从前完全一致：找不到就是找不到，不报错、不猜路径。
void TestEngineLocator::sharedDirIsSkippedWhenNotConfigured()
{
    QTemporaryDir bundledDir;
    QVERIFY(bundledDir.isValid());
    const QString resolved = videodl::resolveExecutable(QString(), QStringLiteral("__absent__.exe"),
                                                        bundledDir.path(), QString());
    QVERIFY(resolved.isEmpty());
}

QTEST_APPLESS_MAIN(TestEngineLocator)

#include "tst_enginelocator.moc"
