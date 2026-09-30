#include "core/DouyinSupport.h"

#include <QTest>

// 抖音专线的站点判定、画质档位映射，以及从渲染好的 DOM 里取字段的规则。
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
};

void TestDouyinSupport::isDouyinUrlCoversShareDomains()
{
    QVERIFY(videodl::isDouyinUrl(
        QStringLiteral("https://www.douyin.com/video/7123456789012345678")));
    // App 里分享出来的是短链，域名不一样。
    QVERIFY(videodl::isDouyinUrl(QStringLiteral("https://v.douyin.com/iRNBho6u/")));
    QVERIFY(videodl::isDouyinUrl(QStringLiteral("https://WWW.DOUYIN.COM/video/1")));

    QVERIFY(!videodl::isDouyinUrl(
        QStringLiteral("https://www.bilibili.com/video/BV1GJ411x7h7")));
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
    const QString dom = QStringLiteral(
        "<html><head><title>  一条视频的标题 - 抖音 </title></head></html>");

    QCOMPARE(videodl::parseDouyinTitle(dom), QStringLiteral("一条视频的标题"));
}

void TestDouyinSupport::parseDouyinTitleKeepsTitleWithoutSuffix()
{
    QCOMPARE(videodl::parseDouyinTitle(QStringLiteral("<title>Only a title</title>")),
             QStringLiteral("Only a title"));
    QVERIFY(videodl::parseDouyinTitle(QStringLiteral("<html></html>")).isEmpty());
}

QTEST_APPLESS_MAIN(TestDouyinSupport)

#include "tst_douyinsupport.moc"
