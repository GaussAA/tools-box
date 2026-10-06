#include "core/EngineFetchPolicy.h"

#include <QTest>

// 内核下载「传输结束后怎么办」的决策表。
//
// 这几条每一条都对应过一次真实故障（Range 收到 200、206 起点错位、取消被当成
// 失败上报），原先埋在 EngineFetcher 里只能靠真下载验证；现在纯函数化后逐条钉死。
class TestEngineFetchPolicy : public QObject
{
    Q_OBJECT

private slots:
    void completesWhenNothingWentWrong();
    void completesWhenRangeResumedExactly();
    void restartsWhenServerIgnoresRange();
    void restartsWhenContentRangeStartMismatches();
    void restartsWhenContentRangeIsMissingOrGarbage();
    void retriesWhileAttemptsRemain();
    void failsAfterMaxAttempts();
    void cancelWinsOverEverythingElse();
    void transportErrorWithFiveHundredDoesNotRestartWhole();
    void fullDownloadWithTwoHundredCompletes();
};

void TestEngineFetchPolicy::completesWhenNothingWentWrong()
{
    videodl::FetchFacts facts;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Complete);
}

void TestEngineFetchPolicy::completesWhenRangeResumedExactly()
{
    // 正常续传：带 Range 收到 206，且起点与 .part 已有长度一致 → 继续收。
    videodl::FetchFacts facts;
    facts.offset = 1024;
    facts.statusCode = 206;
    facts.contentRangeStart = 1024;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Complete);
}

void TestEngineFetchPolicy::restartsWhenServerIgnoresRange()
{
    // 带 Range 却收到 200：服务端从零开始给，半截已对不上。
    videodl::FetchFacts facts;
    facts.offset = 2048;
    facts.statusCode = 200;
    facts.contentRangeStart = -1;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::RestartWhole);
}

void TestEngineFetchPolicy::restartsWhenContentRangeStartMismatches()
{
    // 206 但起点不是我们请求的那个偏移：拼上去就是静默损坏。
    videodl::FetchFacts facts;
    facts.offset = 2048;
    facts.statusCode = 206;
    facts.contentRangeStart = 0;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::RestartWhole);
}

void TestEngineFetchPolicy::restartsWhenContentRangeIsMissingOrGarbage()
{
    // 206 却没有可解析的 Content-Range：无法确认起点，只能重头。
    videodl::FetchFacts facts;
    facts.offset = 512;
    facts.statusCode = 206;
    facts.contentRangeStart = -1;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::RestartWhole);
}

void TestEngineFetchPolicy::retriesWhileAttemptsRemain()
{
    videodl::FetchFacts facts;
    facts.transportFailed = true;
    facts.attempt = 2;
    facts.maxAttempts = 5;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Retry);

    // 恰好剩最后一次机会时仍要重试（重试后才判失败）。
    facts.attempt = 4;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Retry);
}

void TestEngineFetchPolicy::failsAfterMaxAttempts()
{
    videodl::FetchFacts facts;
    facts.transportFailed = true;
    facts.attempt = 5;
    facts.maxAttempts = 5;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Fail);
}

void TestEngineFetchPolicy::cancelWinsOverEverythingElse()
{
    // 用户主动取消：即便同时有传输错误、次数已满，也必须走 Cancelled 而非 Fail ——
    // 「用户中止」与「出错」必须分开报，否则用户以为数据可能坏了。
    videodl::FetchFacts facts;
    facts.userCancelled = true;
    facts.transportFailed = true;
    facts.attempt = 5;
    facts.maxAttempts = 5;
    facts.offset = 1024;
    facts.statusCode = 200;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Cancelled);
}

void TestEngineFetchPolicy::transportErrorWithFiveHundredDoesNotRestartWhole()
{
    // 500 时 statusCode 可能是 500 而非 200，但传输是失败的 —— 此时应重试/失败，
    // 不能误判成「服务端不支持续传」而白丢已下载的部分。
    videodl::FetchFacts facts;
    facts.transportFailed = true;
    facts.offset = 4096;
    facts.statusCode = 500;
    facts.attempt = 1;
    facts.maxAttempts = 5;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Retry);
}

void TestEngineFetchPolicy::fullDownloadWithTwoHundredCompletes()
{
    // 全量下载（offset 为 0）收到 200 是正常完成，别当成需要重头。
    videodl::FetchFacts facts;
    facts.offset = 0;
    facts.statusCode = 200;
    QCOMPARE(videodl::decideFetchAction(facts), videodl::FetchAction::Complete);
}

QTEST_APPLESS_MAIN(TestEngineFetchPolicy)

#include "tst_enginefetchpolicy.moc"
