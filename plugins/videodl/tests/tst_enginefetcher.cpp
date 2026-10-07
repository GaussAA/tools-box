// EngineFetcher 的状态机测试：续传 / 重试 / 取消 / 放弃这几条分支。
//
// 这些分支此前唯一的防线是周六那两个真机脚本（workflow §8 把它们列为「必跑」正是
// 因为没有别的办法）。其中「重试等待期间点取消」这条尤其靠不住 —— 它要求手速刚好
// 落在那 1.5 秒里，撞不上就永远发现不了。
//
// 网络经 IEngineTransport 注入成假的之后，每条分支都可以确定地走到：测试直接给出
// 一个 outcome（200 / 206 起点错位 / 网络错误 / 取消），断言的是状态机的下一步动作。

#include "EngineFetcher.h"
#include "IEngineTransport.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {

/// 假传输：不发出任何网络请求，只记下被调用了几次、以及测试要它给什么结果。
class FakeTransport : public IEngineTransport
{
    Q_OBJECT
public:
    void fetch(const EngineFetchRequest &request) override
    {
        m_requests.append(request);
        m_fetchCount++;
    }

    /// 与真的那个行为一致：abort() 之后仍要给出一次 finished，否则调用方收不了尾。
    void abort() override
    {
        m_abortCount++;
        EngineFetchOutcome outcome;
        outcome.transportFailed = true;
        outcome.errorText = QStringLiteral("Operation canceled");
        emit finished(outcome);
    }

    void completeWith(const EngineFetchOutcome &outcome) { emit finished(outcome); }

    int fetchCount() const { return m_fetchCount; }
    int abortCount() const { return m_abortCount; }
    const QList<EngineFetchRequest> &requests() const { return m_requests; }

private:
    QList<EngineFetchRequest> m_requests;
    int m_fetchCount = 0;
    int m_abortCount = 0;
};

/// 造一个 outcome。默认是一次「成功」的传输（200、无错误）。
EngineFetchOutcome outcome(int statusCode = 200, bool failed = false, qint64 rangeStart = -1)
{
    EngineFetchOutcome o;
    o.statusCode = statusCode;
    o.transportFailed = failed;
    o.errorText = failed ? QStringLiteral("connection reset") : QString();
    o.contentRangeStart = rangeStart;
    return o;
}

} // namespace

class TestEngineFetcher : public QObject
{
    Q_OBJECT

private slots:
    void startIssuesOneTransfer();
    void networkErrorSchedulesAnotherAttempt();
    void exhaustingAttemptsReportsFailure();
    void serverIgnoringRangeRestartsFromZero();
    void cancelDuringTransferStopsAndStaysCancelled();
    void cancelDuringRetryDelayDoesNotRestart();
    void resumeStartsFromExistingPartialFile();
};

// 一次 start 只应发起一次传输，且 url 与目标路径要传对。
void TestEngineFetcher::startIssuesOneTransfer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");
    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"), target);

    QCOMPARE(transport.fetchCount(), 1);
    QCOMPARE(transport.requests().at(0).url, QUrl(QStringLiteral("https://example.invalid/x")));
    QVERIFY(transport.requests().at(0).partPath.endsWith(QStringLiteral(".part")));
    QVERIFY(fetcher.isBusy());
}

// 网络错误 → 重试。断言的是「又发起了一次传输」，而不是等那 1.5 秒的定时器：
// 定时器本身是 Qt 的行为，该测的是我们的决定。
void TestEngineFetcher::networkErrorSchedulesAnotherAttempt()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    QSignalSpy status(&fetcher, &EngineFetcher::status);
    QSignalSpy finished(&fetcher, &EngineFetcher::finished);

    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"),
                  dir.path() + QStringLiteral("/yt-dlp.exe"));
    transport.completeWith(outcome(0, true));

    // 还没到放弃的时候：不能发 finished(false)，也不能原地沉默。
    QCOMPARE(finished.count(), 0);
    QVERIFY2(status.count() >= 1, "重试前应给出一句「正在重试」的说明");

    // 等过那 1.5 秒的重试延迟，确认真的又发起了一次。
    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 2);
}

// 重试次数用尽 → 必须报失败。一直重试下去会让界面永远停在忙碌态。
void TestEngineFetcher::exhaustingAttemptsReportsFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    QSignalSpy finished(&fetcher, &EngineFetcher::finished);

    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"),
                  dir.path() + QStringLiteral("/yt-dlp.exe"));

    // kMaxFetchAttempts 是 5：把它全部耗掉。每次都要等过重试延迟。
    for (int i = 0; i < 5 && finished.isEmpty(); ++i) {
        transport.completeWith(outcome(0, true));
        QTest::qWait(2000);
    }

    QVERIFY2(!finished.isEmpty(), "次数用尽后应报失败");
    QCOMPARE(finished.at(0).at(0).toBool(), false);
    QVERIFY2(!fetcher.isBusy(), "报完失败就必须不再是忙碌态");
}

// 服务端无视 Range、回了 200：得把已下载的部分丢掉整个重来，
// 否则把 200 的完整响应追加到已有的 .part 后面，得到的是一个拼接起来的坏文件。
void TestEngineFetcher::serverIgnoringRangeRestartsFromZero()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");
    const QString partPath = target + QStringLiteral(".part");

    // 注意两件事：
    //   * start() 会先删掉旧的 .part（每次 start 都是一次全新下载），所以残骸要在
    //     第一次传输**之后**造；
    //   * RestartWhole 只在 offset > 0 时才成立 —— 从头下载时收到 200 是正常的
    //     「完整下载」，不是「服务端无视 Range」。所以先要走到一次续传。
    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"), target);
    {
        QFile part(partPath);
        QVERIFY(part.open(QIODevice::WriteOnly));
        part.write(QByteArray(4096, 'x'));
    }

    // 先制造一次中断，让状态机进入续传。
    transport.completeWith(outcome(0, true));
    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 2);
    QCOMPARE(transport.requests().at(1).offset, 4096); // 这次确实带着 Range

    // 服务端回了 200（不是 206），说明它压根没理 Range。
    transport.completeWith(outcome(200, false, -1));
    QVERIFY2(!QFile::exists(partPath), "整个重来之前应把残骸删掉");

    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 3);
    QCOMPARE(transport.requests().at(2).offset, 0); // 第三次从头开始
}

// 取消不是失败：不重试，也不发 finished(false)。
void TestEngineFetcher::cancelDuringTransferStopsAndStaysCancelled()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    QSignalSpy finished(&fetcher, &EngineFetcher::finished);

    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"),
                  dir.path() + QStringLiteral("/yt-dlp.exe"));
    fetcher.cancel();

    QCOMPARE(transport.abortCount(), 1);
    QVERIFY2(finished.isEmpty(), "取消不应报失败");
    QVERIFY2(!fetcher.isBusy(), "取消后必须立刻不再是忙碌态");

    // 关键：取消之后哪怕过了重试延迟，也不该又悄悄发起一次传输。
    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 1);
}

// 这条撞的是同一个 bug 的另一半：取消发生在**重试等待的间隙**（此时没有传输在飞），
// 没有 abort 可发，必须就地收尾。少了那个分流，1.5 秒后下载会自己重新开始 ——
// 用户看到的是「我点了取消，它又下起来了」。
void TestEngineFetcher::cancelDuringRetryDelayDoesNotRestart()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"),
                  dir.path() + QStringLiteral("/yt-dlp.exe"));
    transport.completeWith(outcome(0, true)); // 进入重试等待
    fetcher.cancel();                         // 此刻没有传输在飞

    QCOMPARE(transport.abortCount(), 0); // 没有东西可中止
    QVERIFY(!fetcher.isBusy());

    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 1); // 定时器不应把它重新拉起来
}

// 续传起点取自 .part 的实际大小 —— 取错就变成「从错误的偏移继续」，
// 症状是下载完成但文件打不开，与真因隔着好几层。
void TestEngineFetcher::resumeStartsFromExistingPartialFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    FakeTransport transport;
    EngineFetcher fetcher(&transport, nullptr);

    const QString target = dir.path() + QStringLiteral("/yt-dlp.exe");
    fetcher.start(EngineFetcher::Kind::YtDlp, QStringLiteral("https://example.invalid/x"), target);
    // start() 会先清掉旧残骸，所以第一次必然从头开始。
    QCOMPARE(transport.requests().at(0).offset, 0);

    // 模拟「第一次传输已经落盘了一部分然后断了」。
    QFile part(target + QStringLiteral(".part"));
    QVERIFY(part.open(QIODevice::WriteOnly));
    part.write(QByteArray(1234, 'x'));
    part.close();

    transport.completeWith(outcome(0, true)); // 触发重试

    QTest::qWait(2000);
    QCOMPARE(transport.fetchCount(), 2);
    QCOMPARE(transport.requests().at(1).offset, 1234);
}

QTEST_GUILESS_MAIN(TestEngineFetcher)

#include "tst_enginefetcher.moc"
