#include "core/DownloadArgs.h"

#include <QTest>

// 「一次下载翻译成什么命令行」的规则（plugins/videodl/core/DownloadArgs）。
//
// 这些分支以前散在页面的 launchDownload() 里，改一个画质档位只能真下一次载去试。
class TestDownloadArgs : public QObject
{
    Q_OBJECT

private slots:
    void usesDefaultTemplateWithoutTitle();
    void usesSanitizedTitleAndEscapesPercent();
    void audioOnlyAsksForMp3();
    void withoutFfmpegFallsBackToSingleFile();
    void qualitySelectsFormatFilter();
    void addsRefererAndCookiesWhenAsked();
    void urlComesLast();
};

namespace {

/// 一份最省事的输入：有 ffmpeg、最高画质、不特殊。
videodl::DownloadSpec basicSpec()
{
    videodl::DownloadSpec spec;
    spec.url = QStringLiteral("https://example.com/v");
    spec.outputDir = QStringLiteral("C:/out");
    spec.hasFfmpeg = true;
    spec.ffmpegDir = QStringLiteral("C:/engines/bin");
    return spec;
}

bool containsSequence(const QStringList &args, const QString &first, const QString &second)
{
    for (int i = 0; i + 1 < args.size(); ++i) {
        if (args.at(i) == first && args.at(i + 1) == second) {
            return true;
        }
    }
    return false;
}

} // namespace

void TestDownloadArgs::usesDefaultTemplateWithoutTitle()
{
    const QStringList args = videodl::buildYtDlpArgs(basicSpec());

    // 没给标题就用 yt-dlp 自己那套命名模板，别自己拼。
    QVERIFY(
        containsSequence(args, QStringLiteral("-o"), QStringLiteral("%(title)s [%(id)s].%(ext)s")));
}

void TestDownloadArgs::usesSanitizedTitleAndEscapesPercent()
{
    videodl::DownloadSpec spec = basicSpec();
    spec.title = QStringLiteral("标题 100% 完整");

    const QStringList args = videodl::buildYtDlpArgs(spec);

    // 百分号在 yt-dlp 的输出模板里有含义，必须转义成 %%，否则模板会被吃掉。
    QVERIFY(
        containsSequence(args, QStringLiteral("-o"), QStringLiteral("标题 100%% 完整.%(ext)s")));
}

void TestDownloadArgs::audioOnlyAsksForMp3()
{
    videodl::DownloadSpec spec = basicSpec();
    spec.quality = 4;
    spec.audioOnly = true;

    const QStringList args = videodl::buildYtDlpArgs(spec);

    QVERIFY(containsSequence(args, QStringLiteral("-x"), QStringLiteral("--audio-format")));
    QVERIFY(args.contains(QStringLiteral("mp3")));
    // 仅音频时不该再挑视频清晰度。
    QVERIFY(!args.contains(QStringLiteral("-f")));
}

void TestDownloadArgs::withoutFfmpegFallsBackToSingleFile()
{
    videodl::DownloadSpec spec = basicSpec();
    spec.hasFfmpeg = false;
    spec.ffmpegDir.clear();

    const QStringList args = videodl::buildYtDlpArgs(spec);

    // 没有 ffmpeg 就合不了分轨，只能要「最佳单文件」——代价是清晰度会掉。
    QVERIFY(containsSequence(args, QStringLiteral("-f"), QStringLiteral("b")));
    QVERIFY(!args.contains(QStringLiteral("--merge-output-format")));
    QVERIFY(!args.contains(QStringLiteral("--ffmpeg-location")));
}

void TestDownloadArgs::qualitySelectsFormatFilter()
{
    const auto filterFor = [](int quality) {
        videodl::DownloadSpec spec = basicSpec();
        spec.quality = quality;
        const QStringList args = videodl::buildYtDlpArgs(spec);
        return args.at(args.indexOf(QStringLiteral("-f")) + 1);
    };

    QCOMPARE(filterFor(0), QStringLiteral("bv*+ba/b"));
    QCOMPARE(filterFor(1), QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]"));
    QCOMPARE(filterFor(2), QStringLiteral("bv*[height<=720]+ba/b[height<=720]"));
    QCOMPARE(filterFor(3), QStringLiteral("bv*[height<=480]+ba/b[height<=480]"));
}

void TestDownloadArgs::addsRefererAndCookiesWhenAsked()
{
    videodl::DownloadSpec spec = basicSpec();
    spec.needsReferer = true;
    spec.cookiesPath = QStringLiteral("C:/tmp/cookies.txt");

    const QStringList args = videodl::buildYtDlpArgs(spec);

    // 抖音的 CDN 会检查来源，少这个头就 403。
    QVERIFY(containsSequence(args, QStringLiteral("--referer"),
                             QStringLiteral("https://www.douyin.com/")));
    QVERIFY(
        containsSequence(args, QStringLiteral("--cookies"), QStringLiteral("C:/tmp/cookies.txt")));
}

void TestDownloadArgs::urlComesLast()
{
    // yt-dlp 允许参数与 URL 混排，但 URL 放最后是它的惯例，也让日志更好读。
    const QStringList args = videodl::buildYtDlpArgs(basicSpec());

    QCOMPARE(args.last(), QStringLiteral("https://example.com/v"));
    QVERIFY(containsSequence(args, QStringLiteral("-P"), QStringLiteral("C:/out")));
    QVERIFY(args.contains(QStringLiteral("--no-playlist"))); // 只下当前这一集，必须显式声明
}

QTEST_APPLESS_MAIN(TestDownloadArgs)

#include "tst_downloadargs.moc"
