// DownloadRunner 的编排测试：验证「yt-dlp 的哪一行输出对应哪个界面信号」。
//
// 这一段此前没有任何自动化覆盖 —— 它原先只在真机下载时才会被走到，而「合并阶段要
// 切不确定态」「末行没有换行」这类分支在真机上都是偶发的，靠手点复现基本靠运气。
//
// 子进程经 IChildProcess 注入成假的之后，测试喂的是预置字节、断言的是信号，
// **既不启动子进程也不碰网络**（workflow §5 的约定仍然成立）。

#include "DownloadRunner.h"
#include "IChildProcess.h"

#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

// 假的外部进程。刻意不做成 QProcess 的替身：它只实现 IChildProcess 声明的那几个
// 动作，喂什么输出、怎么结束全由测试说了算。
class FakeProcess : public IChildProcess
{
    Q_OBJECT
public:
    void setChannelMode(ChannelMode) override {}
    void setEnvironment(const QProcessEnvironment &) override {}

    void start(const QString &program, const QStringList &) override
    {
        m_started = true;
        m_program = program;
        m_running = true;
    }
    void kill() override
    {
        m_killCount++;
        m_running = false;
    }
    bool isRunning() const override { return m_running; }

    // 与真的 QProcess 一样「读走即清空」：不清空的话 finish 时那次收尾读取会把
    // 同一段输出再处理一遍，症状是日志里出现重复行。
    QByteArray readAllStandardOutput() override { return take(m_stdout); }
    QByteArray readAllStandardError() override { return take(m_stderr); }
    QString errorString() const override { return m_errorText; }

    void pushOutput(const QByteArray &data)
    {
        m_stdout += data;
        emit readyReadStandardOutput();
    }
    void finishWith(int code, QProcess::ExitStatus status = QProcess::NormalExit)
    {
        m_running = false;
        emit finished(code, status);
    }

    bool started() const { return m_started; }
    int killCount() const { return m_killCount; }
    const QString &program() const { return m_program; }

private:
    static QByteArray take(QByteArray &from)
    {
        const QByteArray value = from;
        from.clear();
        return value;
    }

    QByteArray m_stdout;
    QByteArray m_stderr;
    QString m_program;
    QString m_errorText;
    bool m_started = false;
    bool m_running = false;
    int m_killCount = 0;
};

class TestDownloadRunner : public QObject
{
    Q_OBJECT

private slots:
    void progressLineEmitsPercentAndStatus();
    void mergingLineSwitchesToIndeterminateProgress();
    void lineWithoutTrailingNewlineIsFlushedAtFinish();
    void finishedReportsExitCodeAndOutputPath();
    void cancelKillsTheProcess();
    void crashExitIsReportedAsCrashed();
};

// 进度行 → 百分比 + 一句状态。这是界面上唯一能说明「还活着」的东西。
void TestDownloadRunner::progressLineEmitsPercentAndStatus()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    QSignalSpy progress(&runner, &DownloadRunner::progress);
    QSignalSpy status(&runner, &DownloadRunner::status);
    QSignalSpy logLine(&runner, &DownloadRunner::logLine);

    runner.start(QStringLiteral("yt-dlp"), {QStringLiteral("some-url")});
    QVERIFY2(process.started(), "start() 应把进程拉起来");
    QCOMPARE(process.program(), QStringLiteral("yt-dlp"));

    process.pushOutput("[download]  45.3% of   10.00MiB at    1.23MiB/s ETA 00:12\n");

    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.at(0).at(0).toInt(), 45);
    QCOMPARE(status.count(), 1);
    QVERIFY2(!status.at(0).at(0).toString().isEmpty(), "进度行应给出一句状态说明");
    // 每一行输出都要进日志面板，否则用户看到的状态与日志对不上。
    QCOMPARE(logLine.count(), 1);
}

// 合并阶段必须切到不确定态（-1）。这条在真机上极难复现，但踩中的后果很典型：
// 进度条停在 100% 十几秒到几分钟，看着像卡死。
void TestDownloadRunner::mergingLineSwitchesToIndeterminateProgress()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    QSignalSpy progress(&runner, &DownloadRunner::progress);
    QSignalSpy status(&runner, &DownloadRunner::status);

    runner.start(QStringLiteral("yt-dlp"), {});
    process.pushOutput("[Merger] Merging formats into \"a.mp4\"\n");

    QCOMPARE(progress.count(), 1);
    QCOMPARE(progress.at(0).at(0).toInt(), -1);
    QVERIFY(status.at(0).at(0).toString().contains(QStringLiteral("合并")));
}

// 最后一行常常没有换行，不 flush 就永远取不到 —— 而它偏偏经常就是
// 「Destination: xxx.mp4」那一行，丢了就不知道文件存哪了。
void TestDownloadRunner::lineWithoutTrailingNewlineIsFlushedAtFinish()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    QSignalSpy finished(&runner, &DownloadRunner::finished);

    runner.start(QStringLiteral("yt-dlp"), {});
    process.pushOutput("[download] Destination: out.mp4"); // 故意不带换行
    QCOMPARE(finished.count(), 0);

    process.finishWith(0);

    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("out.mp4"));
}

// finished 要带齐 exitCode / crashed / outputPath 三件事，页面靠它们区分
// 「成功」与「失败」并给出不同的文案。
void TestDownloadRunner::finishedReportsExitCodeAndOutputPath()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    QSignalSpy finished(&runner, &DownloadRunner::finished);

    runner.start(QStringLiteral("yt-dlp"), {});
    process.pushOutput("[download] Destination: a.mp4\n");
    process.finishWith(3);

    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(0).toInt(), 3);
    QCOMPARE(finished.at(0).at(1).toBool(), false); // 非零退出但不是崩溃
    QCOMPARE(finished.at(0).at(2).toString(), QStringLiteral("a.mp4"));
}

// 取消必须真的去杀进程：只置个标志的话，yt-dlp 会在后台继续写文件。
void TestDownloadRunner::cancelKillsTheProcess()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    runner.start(QStringLiteral("yt-dlp"), {});
    QVERIFY(runner.isRunning());

    runner.cancel();
    QCOMPARE(process.killCount(), 1);
    QVERIFY(!runner.isRunning());
}

// 崩溃与「非零退出」是两回事：页面据此给不同文案（崩溃往往意味着内核有问题）。
void TestDownloadRunner::crashExitIsReportedAsCrashed()
{
    FakeProcess process;
    DownloadRunner runner(&process);

    QSignalSpy finished(&runner, &DownloadRunner::finished);

    runner.start(QStringLiteral("yt-dlp"), {});
    process.finishWith(0, QProcess::CrashExit);

    QCOMPARE(finished.count(), 1);
    QCOMPARE(finished.at(0).at(1).toBool(), true);
}

QTEST_GUILESS_MAIN(TestDownloadRunner)

#include "tst_downloadrunner.moc"
