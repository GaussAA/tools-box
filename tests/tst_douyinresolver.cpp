// DouyinResolver 的编排测试：渲染结束之后，哪一种结局该报哪一种结果。
//
// 结局判定的**顺序**本身在 core/DouyinSupport（有单测）。剩下没覆盖的是编排这一侧：
// DOM 收全了没有、浏览器起不来时有没有把真因（errorString）带给用户、拿到 video_id
// 之后有没有拼出直链。这几条此前只能靠真机渲染去撞。
//
// 子进程经 IChildProcess 注入成假的之后，测试喂的是预置 DOM 与退出码，不启动浏览器。

#include "DouyinResolver.h"
#include "IChildProcess.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <utility>

namespace {

/// 假的渲染进程：不启动浏览器，只让测试喂 DOM 与退出码。
class FakeRenderer : public IChildProcess
{
    Q_OBJECT
public:
    void setChannelMode(ChannelMode) override {}
    void setEnvironment(const QProcessEnvironment &) override {}

    void start(const QString &, const QStringList &args) override
    {
        m_started = true;
        m_args = args;
        m_running = true;
    }
    void kill() override
    {
        m_killed = true;
        m_running = false;
    }
    bool isRunning() const override { return m_running; }
    QByteArray readAllStandardOutput() override { return take(m_stdout); }
    QByteArray readAllStandardError() override { return take(m_stderr); }
    QString errorString() const override { return m_errorText; }

    void pushDom(const QByteArray &data)
    {
        m_stdout += data;
        emit readyReadStandardOutput();
    }
    void finishWith(int code, QProcess::ExitStatus status = QProcess::NormalExit)
    {
        m_running = false;
        emit finished(code, status);
    }
    void failToStart(const QString &why)
    {
        m_running = false;
        m_errorText = why;
        emit errorOccurred(QProcess::FailedToStart);
    }

    bool started() const { return m_started; }
    bool killed() const { return m_killed; }
    const QStringList &args() const { return m_args; }

private:
    static QByteArray take(QByteArray &from)
    {
        const QByteArray value = from;
        from.clear();
        return value;
    }

    QByteArray m_stdout;
    QByteArray m_stderr;
    QStringList m_args;
    QString m_errorText;
    bool m_started = false;
    bool m_running = false;
    bool m_killed = false;
};

/// 造一台「确实装了浏览器」的机器，路径经 outPath 返回。
///
/// 必须是**真实存在的文件**：core 的 pickHeadlessBrowser 用 QFileInfo::exists 挑候选，
/// 给一个不存在的路径会被当成「这台机器上没有浏览器」。
bool createFakeBrowser(const QString &dir, QString &outPath)
{
    outPath = dir + QStringLiteral("/msedge.exe");
    QFile browser(outPath);
    if (!browser.open(QIODevice::WriteOnly)) {
        return false;
    }
    browser.close();
    return true;
}

/// 给一台「确实装了浏览器」的机器：生产实现读环境变量，而测试进程里的 ProgramFiles
/// 未必指向任何真实目录，不覆盖它的话每个用例都会先撞上「未找到浏览器」。
class ResolverWithBrowser : public DouyinResolver
{
public:
    ResolverWithBrowser(IChildProcess *process, QString browserPath)
        : DouyinResolver(process)
        , m_browser(std::move(browserPath))
    {}

protected:
    QStringList browserCandidates() const override { return QStringList{m_browser}; }

private:
    QString m_browser;
};

/// 造一份「渲染完成」的 DOM：video_id 只有等抖音自己的页面脚本跑完才会出现。
QByteArray domWithVideoId(const QString &videoId = QStringLiteral("1234567890123456"))
{
    return QStringLiteral("<html><head><title>某条视频 - 抖音</title></head>"
                          "<body>video_id=%1</body></html>")
        .arg(videoId)
        .toUtf8();
}

} // namespace

class TestDouyinResolver : public QObject
{
    Q_OBJECT

private slots:
    void noBrowserReportsFailureWithoutStartingAnything();
    void domWithVideoIdResolvesPlayUrlAndTitle();
    void browserFailedToStartCarriesTheRealReason();
    void nonZeroExitReportsFailure();
    void domWithoutVideoIdReportsNoVideoId();
    void cancelKillsTheRenderer();
};

// 机器上没有浏览器时应当立刻失败，而不是先起一个注定起不来的进程。
void TestDouyinResolver::noBrowserReportsFailureWithoutStartingAnything()
{
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, QStringLiteral("C:/definitely/not/here/msedge.exe"));

    QSignalSpy failed(&resolver, &DouyinResolver::failed);
    QSignalSpy status(&resolver, &DouyinResolver::status);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);

    QCOMPARE(failed.count(), 1);
    QVERIFY2(!renderer.started(), "没有浏览器时不应尝试启动进程");
    QVERIFY2(status.count() >= 1, "应给出一句「未找到浏览器」的说明");
}

// 拿到 video_id → 拼出直链。这条链路走错（少了 line、ratio 空串）的症状是
// 「下载莫名失败」，与真正的原因隔着好几层。
void TestDouyinResolver::domWithVideoIdResolvesPlayUrlAndTitle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString browser;
    QVERIFY(createFakeBrowser(dir.path(), browser));
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, browser);

    QSignalSpy resolved(&resolver, &DouyinResolver::resolved);
    QSignalSpy failed(&resolver, &DouyinResolver::failed);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);
    QVERIFY2(renderer.started(), "找到浏览器后应启动渲染");

    renderer.pushDom(domWithVideoId());
    renderer.finishWith(0);

    QCOMPARE(failed.count(), 0);
    QCOMPARE(resolved.count(), 1);
    const QString playUrl = resolved.at(0).at(0).toString();
    QVERIFY2(playUrl.contains(QStringLiteral("1234567890123456")), "直链里应带上 video_id");
    QVERIFY2(!playUrl.isEmpty(), "直链不应为空");
    // 标题要去掉「 - 抖音」后缀，否则用户看到的标题带着站点名。
    QCOMPARE(resolved.at(0).at(1).toString(), QStringLiteral("某条视频"));
}

// 浏览器起不来时，必须把 errorString 原样带给用户 —— 光说「无法启动」等于什么都没说
// （真因可能是路径失效、权限、或被安全软件拦下，而这几种的处理方式完全不同）。
void TestDouyinResolver::browserFailedToStartCarriesTheRealReason()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString browser;
    QVERIFY(createFakeBrowser(dir.path(), browser));
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, browser);

    QSignalSpy failed(&resolver, &DouyinResolver::failed);
    QSignalSpy logLine(&resolver, &DouyinResolver::logLine);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);
    renderer.failToStart(QStringLiteral("被安全策略拦下了"));

    QCOMPARE(failed.count(), 1);
    bool reasonCarried = false;
    for (const QVariantList &entry : logLine) {
        if (entry.at(0).toString().contains(QStringLiteral("被安全策略拦下了"))) {
            reasonCarried = true;
        }
    }
    QVERIFY2(reasonCarried, "失败日志里应带上 errorString 给出的真因");
}

// 非零退出 → 失败。注意它**不能**被报成超时：那是把真因吃掉的那次故障的形态。
void TestDouyinResolver::nonZeroExitReportsFailure()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString browser;
    QVERIFY(createFakeBrowser(dir.path(), browser));
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, browser);

    QSignalSpy failed(&resolver, &DouyinResolver::failed);
    QSignalSpy resolved(&resolver, &DouyinResolver::resolved);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);
    renderer.finishWith(3);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(resolved.count(), 0);
}

// 渲染正常结束但 DOM 里没有 video_id —— 多半是抖音改版了。这条必须单独报，
// 否则用户看到的是「渲染成功但没反应」。
void TestDouyinResolver::domWithoutVideoIdReportsNoVideoId()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString browser;
    QVERIFY(createFakeBrowser(dir.path(), browser));
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, browser);

    QSignalSpy failed(&resolver, &DouyinResolver::failed);
    QSignalSpy resolved(&resolver, &DouyinResolver::resolved);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);
    renderer.pushDom("<html><body>什么都没有</body></html>");
    renderer.finishWith(0);

    QCOMPARE(failed.count(), 1);
    QCOMPARE(resolved.count(), 0);
}

// 取消要真的去杀渲染进程：无头浏览器不杀会在后台一直跑。
void TestDouyinResolver::cancelKillsTheRenderer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString browser;
    QVERIFY(createFakeBrowser(dir.path(), browser));
    FakeRenderer renderer;
    ResolverWithBrowser resolver(&renderer, browser);

    resolver.start(QStringLiteral("https://www.douyin.com/video/1"), 0);
    QVERIFY(resolver.isRunning());

    resolver.cancel();
    QVERIFY2(renderer.killed(), "取消应杀掉渲染进程");
}

QTEST_GUILESS_MAIN(TestDouyinResolver)

#include "tst_douyinresolver.moc"
