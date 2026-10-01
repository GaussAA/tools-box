#include "core/OutputParsing.h"

#include <QTest>

// yt-dlp 输出流的解析规则。
//
// 这些规则以前散在 VideoDlPlugin.cpp 里，只能靠真跑一次下载去验证；现在用
// 真实输出样例行就能覆盖（见 docs/architecture.md §3）。
class TestOutputParsing : public QObject
{
    Q_OBJECT

private slots:
    void decodeOutputKeepsValidUtf8();
    void decodeOutputFallsBackToLocalEncoding();
    void stripAnsiRemovesColorSequences();
    void extractUrlPullsAddressOutOfShareText();
    void extractUrlStopsAtNonAsciiCharacters();
    void extractUrlReturnsInputWhenNothingFound();
    void parseProgressReadsPercentSpeedAndEta();
    void parseProgressWithoutSpeedOrEta();
    void parseProgressIgnoresOtherLines();
    void classifyStageRecognisesStages();
    void parseDestinationReadsFinalArtifact();
    void parseDestinationIgnoresOtherLines();
};

void TestOutputParsing::decodeOutputKeepsValidUtf8()
{
    const QByteArray bytes = QStringLiteral("中文标题").toUtf8();
    QCOMPARE(videodl::decodeOutput(bytes), QStringLiteral("中文标题"));
}

void TestOutputParsing::decodeOutputFallsBackToLocalEncoding()
{
#ifdef Q_OS_WIN
    // GBK 编码的「你好」不是合法 UTF-8，必须退回本地代码页才能解出汉字，
    // 而不是留下一串替换字符。
    const QByteArray gbk("\xC4\xE3\xBA\xC3", 4);

    const QString decoded = videodl::decodeOutput(gbk);
    QVERIFY(decoded != QString::fromUtf8(gbk));
    QVERIFY(!decoded.contains(QChar::ReplacementCharacter));
#else
    QSKIP("本地代码页回退只在 Windows 控制台环境下才有意义");
#endif
}

void TestOutputParsing::stripAnsiRemovesColorSequences()
{
    QCOMPARE(videodl::stripAnsi(QStringLiteral("\x1b[0;32m[download] 50.0%\x1b[0m")),
             QStringLiteral("[download] 50.0%"));

    // 没有转义序列的行必须原样返回。
    QCOMPARE(videodl::stripAnsi(QStringLiteral("plain text")), QStringLiteral("plain text"));
}

void TestOutputParsing::extractUrlPullsAddressOutOfShareText()
{
    const QString share = QStringLiteral(
        "7.43 复制打开抖音，看看【某人的作品】 https://v.douyin.com/iRNBho6u/ 复制此链接");

    QCOMPARE(videodl::extractUrl(share), QStringLiteral("https://v.douyin.com/iRNBho6u/"));
}

void TestOutputParsing::extractUrlStopsAtNonAsciiCharacters()
{
    // 地址后面直接跟中文（没有空格）时，也要在中文处自然截断。
    const QString text = QStringLiteral("https://www.bilibili.com/video/BV1GJ411x7h7/复制此链接");

    QCOMPARE(videodl::extractUrl(text),
             QStringLiteral("https://www.bilibili.com/video/BV1GJ411x7h7/"));
}

void TestOutputParsing::extractUrlReturnsInputWhenNothingFound()
{
    // 抽不到地址时原样返回，交给下游去报「地址无效」。
    QCOMPARE(videodl::extractUrl(QStringLiteral("BV1GJ411x7h7")), QStringLiteral("BV1GJ411x7h7"));
}

void TestOutputParsing::parseProgressReadsPercentSpeedAndEta()
{
    const videodl::ProgressInfo info = videodl::parseProgress(
        QStringLiteral("[download]  45.3% of   10.00MiB at    1.23MiB/s ETA 00:12"));

    QVERIFY(info.matched);
    QCOMPARE(info.percent, 45);
    QCOMPARE(info.speed, QStringLiteral("1.23MiB/s"));
    QCOMPARE(info.eta, QStringLiteral("00:12"));
}

void TestOutputParsing::parseProgressWithoutSpeedOrEta()
{
    const videodl::ProgressInfo info =
        videodl::parseProgress(QStringLiteral("[download]  10.0% of ~  1.00MiB"));

    QVERIFY(info.matched);
    QCOMPARE(info.percent, 10);
    QVERIFY(info.speed.isEmpty());
    QVERIFY(info.eta.isEmpty());
}

void TestOutputParsing::parseProgressIgnoresOtherLines()
{
    QVERIFY(!videodl::parseProgress(QStringLiteral("[download] Destination: a.mp4")).matched);
    QVERIFY(
        !videodl::parseProgress(QStringLiteral("[Merger] Merging formats into \"a.mp4\"")).matched);
}

void TestOutputParsing::classifyStageRecognisesStages()
{
    QCOMPARE(videodl::classifyStage(QStringLiteral("[download] Destination: a.mp4")),
             videodl::OutputStage::DownloadStarting);
    QCOMPARE(videodl::classifyStage(QStringLiteral("[Merger] Merging formats into \"a.mp4\"")),
             videodl::OutputStage::Merging);
    QCOMPARE(videodl::classifyStage(QStringLiteral("[ExtractAudio] Destination: a.mp3")),
             videodl::OutputStage::ExtractingAudio);
    QCOMPARE(videodl::classifyStage(QStringLiteral("[ffmpeg] Adding thumbnail")),
             videodl::OutputStage::ExtractingAudio);
    QCOMPARE(videodl::classifyStage(QStringLiteral("some other line")), videodl::OutputStage::None);
}

void TestOutputParsing::parseDestinationReadsFinalArtifact()
{
    QCOMPARE(videodl::parseDestination(QStringLiteral("[download] Destination: C:\\v\\a.mp4")),
             QStringLiteral("C:\\v\\a.mp4"));
    QCOMPARE(
        videodl::parseDestination(QStringLiteral("[Merger] Merging formats into \"C:\\v\\a.mp4\"")),
        QStringLiteral("C:\\v\\a.mp4"));
    QCOMPARE(videodl::parseDestination(QStringLiteral("[ExtractAudio] Destination: out.mp3")),
             QStringLiteral("out.mp3"));
}

void TestOutputParsing::parseDestinationIgnoresOtherLines()
{
    QVERIFY(videodl::parseDestination(QStringLiteral("[download]  45.3% of 10.00MiB")).isEmpty());
}

QTEST_APPLESS_MAIN(TestOutputParsing)

#include "tst_outputparsing.moc"
