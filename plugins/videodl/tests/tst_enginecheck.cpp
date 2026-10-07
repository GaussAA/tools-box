#include "core/EngineCheck.h"

#include <QTest>

// 内核下载落地后的最小完整性校验（偏差 9.12）：拦「下载到 HTML 错误页」与
// 「严重截断的文件」两类最常见的损坏。这里把三类输入各过一遍：正常头 + 足量、
// 错误头、正常头但不足量；外加真实错误页的头部字节。
class TestEngineCheck : public QObject
{
    Q_OBJECT

private slots:
    void acceptsPlausibleYtDlp();
    void rejectsHtmlErrorPageForYtDlp();
    void rejectsTruncatedYtDlp();
    void acceptsPlausibleFfmpegZip();
    void rejectsNonZipForFfmpeg();
    void rejectsTruncatedFfmpegZip();
    void emptyHeadIsBadHeader();
};

namespace {

/// 与下限无关的 PE 魔数。
const QByteArray kPe = QByteArrayLiteral("MZ");

/// 与下限无关的 Zip 魔数。
const QByteArray kZip = QByteArrayLiteral("PK\x03\x04");

/// yt-dlp.exe 的可信下限，与 core/EngineCheck.cpp 里的实现同值。
constexpr qint64 kMinYtDlp = 8 * 1024 * 1024;

/// ffmpeg 压缩包的可信下限，与 core/EngineCheck.cpp 里的实现同值。
constexpr qint64 kMinZip = 32 * 1024 * 1024;

} // namespace

void TestEngineCheck::acceptsPlausibleYtDlp()
{
    QCOMPARE(videodl::checkYtDlpBinary(kMinYtDlp, kPe), videodl::EngineFileProblem::None);
    // 远大于下限的真实体量（约 18 MB）同样通过。
    QCOMPARE(videodl::checkYtDlpBinary(18 * 1024 * 1024, kPe), videodl::EngineFileProblem::None);
}

void TestEngineCheck::rejectsHtmlErrorPageForYtDlp()
{
    // 404 / 反爬页的开头：大小再大也拦。
    const QByteArray html = QByteArrayLiteral("<!DOCTYPE html>");
    QCOMPARE(videodl::checkYtDlpBinary(20 * 1024 * 1024, html),
             videodl::EngineFileProblem::BadHeader);
}

void TestEngineCheck::rejectsTruncatedYtDlp()
{
    // 头对、但大小在下限之下：按截断处理。
    QCOMPARE(videodl::checkYtDlpBinary(kMinYtDlp - 1, kPe), videodl::EngineFileProblem::TooSmall);
}

void TestEngineCheck::acceptsPlausibleFfmpegZip()
{
    QCOMPARE(videodl::checkFfmpegZip(kMinZip, kZip), videodl::EngineFileProblem::None);
}

void TestEngineCheck::rejectsNonZipForFfmpeg()
{
    // 把 yt-dlp 下成了 exe、却拿去当 zip 解压，这类张冠李戴也要拦。
    QCOMPARE(videodl::checkFfmpegZip(80 * 1024 * 1024, kPe), videodl::EngineFileProblem::BadHeader);
}

void TestEngineCheck::rejectsTruncatedFfmpegZip()
{
    QCOMPARE(videodl::checkFfmpegZip(kMinZip - 1, kZip), videodl::EngineFileProblem::TooSmall);
}

void TestEngineCheck::emptyHeadIsBadHeader()
{
    // 读不到任何字节（磁盘满、文件消失等）按坏头处理，宁可错杀不放进解压。
    QCOMPARE(videodl::checkYtDlpBinary(kMinYtDlp, QByteArray()),
             videodl::EngineFileProblem::BadHeader);
    QCOMPARE(videodl::checkFfmpegZip(kMinZip, QByteArray()), videodl::EngineFileProblem::BadHeader);
}

QTEST_APPLESS_MAIN(TestEngineCheck)

#include "tst_enginecheck.moc"
