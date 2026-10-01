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

QTEST_APPLESS_MAIN(TestEngineLocator)

#include "tst_enginelocator.moc"
