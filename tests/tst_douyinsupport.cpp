#include "core/DouyinSupport.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// 抖音专线的站点判定、画质档位映射，以及从渲染好的 DOM 里取字段的规则。
namespace {

/// 建一个空文件当「存在的候选」，返回路径；失败返回空串。
QString writeTouchFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return QString();
    }
    file.close();
    return path;
}

} // namespace

class TestDouyinSupport : public QObject
{
    Q_OBJECT

private slots:
    void isDouyinUrlCoversShareDomains();
    void douyinRatioMapsQualityIndex();
    void douyinRatioFallsBackToHighestForOutOfRange();
    void parseDouyinVideoIdNeedsEnoughCharacters();
    void parseDouyinTitleDropsSiteSuffix();
    void parseDouyinTitleKeepsTitleWithoutSuffix();
    void playUrlCarriesVideoIdAndRatio();
    void playUrlFallsBackToHighestRatioForDirtyQuality();
    void renderOutcomePrefersCancelOverEverything();
    void renderOutcomeReportsStartFailureBeforeTimeout();
    void renderOutcomeReportsTimeoutBeforeCrash();
    void renderOutcomeReportsCrashWhenExitIsNotNormal();
    void renderOutcomeReportsMissingVideoIdOnCleanExit();
    void pickBrowserSkipsPathsFromMissingEnvVars();
    void pickBrowserReturnsFirstExistingCandidate();
    void pickBrowserReturnsEmptyWhenNoneExists();
};

void TestDouyinSupport::isDouyinUrlCoversShareDomains()
{
    QVERIFY(
        videodl::isDouyinUrl(QStringLiteral("https://www.douyin.com/video/7123456789012345678")));
    // App 里分享出来的是短链，域名不一样。
    QVERIFY(videodl::isDouyinUrl(QStringLiteral("https://v.douyin.com/iRNBho6u/")));
    QVERIFY(videodl::isDouyinUrl(QStringLiteral("https://WWW.DOUYIN.COM/video/1")));

    QVERIFY(!videodl::isDouyinUrl(QStringLiteral("https://www.bilibili.com/video/BV1GJ411x7h7")));
}

void TestDouyinSupport::douyinRatioMapsQualityIndex()
{
    QCOMPARE(videodl::douyinRatio(0), QStringLiteral("1080p"));
    QCOMPARE(videodl::douyinRatio(1), QStringLiteral("1080p"));
    QCOMPARE(videodl::douyinRatio(2), QStringLiteral("720p"));
    QCOMPARE(videodl::douyinRatio(3), QStringLiteral("540p"));
    // 「仅音频」也得先取到视频流再由 ffmpeg 抽音轨，所以同样按最高档拿。
    QCOMPARE(videodl::douyinRatio(4), QStringLiteral("1080p"));
}

void TestDouyinSupport::douyinRatioFallsBackToHighestForOutOfRange()
{
    QCOMPARE(videodl::douyinRatio(-1), QStringLiteral("1080p"));
    QCOMPARE(videodl::douyinRatio(99), QStringLiteral("1080p"));
}

void TestDouyinSupport::parseDouyinVideoIdNeedsEnoughCharacters()
{
    const QString dom = QStringLiteral(
        "<script>window.__DATA__ = \"video_id=7123456789012345678&ratio=1080p\";</script>");
    QCOMPARE(videodl::parseDouyinVideoId(dom), QStringLiteral("7123456789012345678"));

    // 长度不够的多半是别的字段，不能拿它去拼播放接口。
    QVERIFY(videodl::parseDouyinVideoId(QStringLiteral("video_id=abc")).isEmpty());
    QVERIFY(videodl::parseDouyinVideoId(QStringLiteral("<html></html>")).isEmpty());
}

void TestDouyinSupport::parseDouyinTitleDropsSiteSuffix()
{
    const QString dom =
        QStringLiteral("<html><head><title>  一条视频的标题 - 抖音 </title></head></html>");

    QCOMPARE(videodl::parseDouyinTitle(dom), QStringLiteral("一条视频的标题"));
}

void TestDouyinSupport::parseDouyinTitleKeepsTitleWithoutSuffix()
{
    QCOMPARE(videodl::parseDouyinTitle(QStringLiteral("<title>Only a title</title>")),
             QStringLiteral("Only a title"));
    QVERIFY(videodl::parseDouyinTitle(QStringLiteral("<html></html>")).isEmpty());
}

void TestDouyinSupport::playUrlCarriesVideoIdAndRatio()
{
    const QString url = videodl::douyinPlayUrl(QStringLiteral("7123456789012345678"), 2);
    QVERIFY(url.startsWith(QStringLiteral("https://www.douyin.com/aweme/v1/play/?")));
    QVERIFY(url.contains(QStringLiteral("video_id=7123456789012345678")));
    QVERIFY(url.contains(QStringLiteral("ratio=720p")));
    // line=0 不能少：缺了它抖音会挑另一条线路，症状是「地址对但取不到流」。
    QVERIFY(url.contains(QStringLiteral("line=0")));
}

void TestDouyinSupport::playUrlFallsBackToHighestRatioForDirtyQuality()
{
    // 画质下标越界时 ratio 仍要是非空的合法值，绝不能拼出 ratio= 空串。
    const QString url = videodl::douyinPlayUrl(QStringLiteral("7123456789012345678"), 99);
    QVERIFY(url.contains(QStringLiteral("ratio=1080p")));
    QVERIFY(!url.contains(QStringLiteral("ratio=&")));
}

void TestDouyinSupport::renderOutcomePrefersCancelOverEverything()
{
    // 用户主动取消优先于任何异常：取消与失败必须分开报。
    QCOMPARE(videodl::classifyDouyinRender(true, true, true, false, false),
             videodl::DouyinRenderOutcome::Cancelled);
}

void TestDouyinSupport::renderOutcomeReportsStartFailureBeforeTimeout()
{
    // 浏览器根本起不来时不能报成「60 秒超时」——那正是把真因吃掉的那次故障。
    QCOMPARE(videodl::classifyDouyinRender(false, true, true, false, false),
             videodl::DouyinRenderOutcome::BrowserFailed);
    QCOMPARE(videodl::classifyDouyinRender(false, true, false, false, false),
             videodl::DouyinRenderOutcome::BrowserFailed);
}

void TestDouyinSupport::renderOutcomeReportsTimeoutBeforeCrash()
{
    // 超时会把进程杀掉，于是退出码必然非零；此时要报超时而不是「渲染失败」。
    QCOMPARE(videodl::classifyDouyinRender(false, false, true, false, false),
             videodl::DouyinRenderOutcome::TimedOut);
}

void TestDouyinSupport::renderOutcomeReportsCrashWhenExitIsNotNormal()
{
    QCOMPARE(videodl::classifyDouyinRender(false, false, false, false, true),
             videodl::DouyinRenderOutcome::BrowserCrashed);
}

void TestDouyinSupport::renderOutcomeReportsMissingVideoIdOnCleanExit()
{
    // 正常退出但 DOM 里没有 video_id：抖音又改版了，与「浏览器挂了」要分开。
    QCOMPARE(videodl::classifyDouyinRender(false, false, false, true, false),
             videodl::DouyinRenderOutcome::NoVideoId);
    QCOMPARE(videodl::classifyDouyinRender(false, false, false, true, true),
             videodl::DouyinRenderOutcome::Resolved);
}

void TestDouyinSupport::pickBrowserSkipsPathsFromMissingEnvVars()
{
    // 环境变量缺失时拼出来的是 "/Microsoft/Edge/..." 这种以 / 开头的怪串，
    // 里面恰好有一项真的存在（当前目录下的 Microsoft/Edge/...）也不能选它。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/Microsoft/Edge/Application")));
    const QString decoy =
        writeTouchFile(dir.path() + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"));
    QVERIFY(!decoy.isEmpty());

    const QString picked = videodl::pickHeadlessBrowser(
        {QStringLiteral("/Microsoft/Edge/Application/msedge.exe"), decoy});

    QCOMPARE(picked, decoy);
}

void TestDouyinSupport::pickBrowserReturnsFirstExistingCandidate()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = writeTouchFile(dir.path() + QStringLiteral("/first.exe"));
    const QString second = writeTouchFile(dir.path() + QStringLiteral("/second.exe"));
    QVERIFY(!first.isEmpty() && !second.isEmpty());

    QCOMPARE(videodl::pickHeadlessBrowser({first, second}), first);
}

void TestDouyinSupport::pickBrowserReturnsEmptyWhenNoneExists()
{
    QCOMPARE(videodl::pickHeadlessBrowser({QStringLiteral("C:/__nope__/msedge.exe")}), QString());
    QCOMPARE(videodl::pickHeadlessBrowser({}), QString());
}

QTEST_APPLESS_MAIN(TestDouyinSupport)

#include "tst_douyinsupport.moc"
