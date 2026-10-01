#include "DouyinResolver.h"

#include "core/DouyinSupport.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>

namespace {

/// 渲染阶段的总超时（毫秒）：抖音的脚本要跑十几秒，60 秒还没动静就是卡住了。
constexpr int kRenderTimeoutMs = 60000;

/// 找一个能用来做无头渲染的浏览器。返回空串表示这台机器上没找到。
QString findHeadlessBrowser()
{
    const QStringList candidates{
        qEnvironmentVariable("ProgramFiles(x86)")
            + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
        qEnvironmentVariable("ProgramFiles")
            + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
        qEnvironmentVariable("LocalAppData")
            + QStringLiteral("/Microsoft/Edge/Application/msedge.exe"),
        qEnvironmentVariable("ProgramFiles")
            + QStringLiteral("/Google/Chrome/Application/chrome.exe"),
        qEnvironmentVariable("ProgramFiles(x86)")
            + QStringLiteral("/Google/Chrome/Application/chrome.exe"),
    };
    for (const QString &path : candidates) {
        if (path.startsWith(QLatin1Char('/'))) {
            continue; // 环境变量缺失时会拼出「/Microsoft/...」这种路径
        }
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

/// 渲染用的独立 profile 目录：用独立的目录，免得去碰用户正在用的浏览器数据。
QString renderProfileDir()
{
    return QDir::tempPath() + QStringLiteral("/toolbox-douyin-render");
}

} // namespace

DouyinResolver::DouyinResolver(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
{
    // 渲染进程要单独收 stdout（整份 DOM 都在那上面），所以不能合并通道。
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &DouyinResolver::onReadyRead);
    connect(m_process, &QProcess::finished, this, &DouyinResolver::onFinished);

    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_startFailed = true;
            onFinished(-1, QProcess::NormalExit);
        }
    });
}

DouyinResolver::~DouyinResolver()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }
    QDir(renderProfileDir()).removeRecursively();
}

bool DouyinResolver::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

void DouyinResolver::start(const QString &url, int quality)
{
    const QString browser = findHeadlessBrowser();
    if (browser.isEmpty()) {
        emit logLine(tr("抖音要靠浏览器渲染页面才能取到播放地址，但这台机器上没找到 "
                        "Edge 或 Chrome。"));
        emit status(tr("未找到可用的浏览器。"));
        emit failed();
        return;
    }

    m_quality = quality;
    m_dom.clear();
    m_timedOut = false;
    m_startFailed = false;
    m_cancelled = false;

    // 每次都用干净的 profile，否则上一轮留下的状态会干扰渲染结果。
    QDir(renderProfileDir()).removeRecursively();

    const QStringList args{
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        // 无头模式默认会在 navigator.webdriver 上暴露自己，抖音认这个，关掉。
        QStringLiteral("--disable-blink-features=AutomationControlled"),
        QStringLiteral("--user-data-dir=") + renderProfileDir(),
        // 抖音的脚本要跑十几秒才会把 video_id 写进 DOM，虚拟时间给足。
        QStringLiteral("--virtual-time-budget=20000"),
        QStringLiteral("--dump-dom"),
        url,
    };

    emit logLine(tr("抖音的播放地址要等页面脚本跑完才出现，正在用浏览器渲染…"));
    emit logLine(tr("渲染器：%1").arg(QDir::toNativeSeparators(browser)));
    emit progress(-1);
    emit status(tr("正在渲染抖音页面…"));

    m_process->start(browser, args);

    // 启动结果一律交给 onFinished 判定：不再 waitForStarted()（那会在 GUI 线程上
    // 阻塞，而且「返回 false」与「errorOccurred(FailedToStart)」两条路径会打出
    // 两条互相矛盾的失败原因）。超时定时器因此从启动就开始计时。
    const int generation = ++m_generation;
    QTimer::singleShot(kRenderTimeoutMs, this, [this, generation] {
        // 带上轮次编号，免得上一次留下的定时器把这一轮刚启动的进程杀掉。
        if (generation == m_generation && m_process->state() != QProcess::NotRunning) {
            m_timedOut = true;
            m_process->kill();
        }
    });
}

void DouyinResolver::cancel()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_cancelled = true;
        m_process->kill();
    }
}

void DouyinResolver::onReadyRead()
{
    m_dom += m_process->readAllStandardOutput();
}

void DouyinResolver::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_dom += m_process->readAllStandardOutput(); // 收尾，别漏掉最后一段

    const bool cancelled = m_cancelled;
    const bool timedOut = m_timedOut;
    const bool startFailed = m_startFailed;
    m_cancelled = false;
    m_timedOut = false;
    m_startFailed = false;

    // 浏览器会在 profile 目录里堆一堆文件，用完就清干净。
    QDir(renderProfileDir()).removeRecursively();

    auto giveUp = [this](const QString &logLine, const QString &statusLine) {
        emit this->logLine(logLine);
        emit status(statusLine);
        emit progress(0);
        emit failed();
    };

    if (cancelled) {
        giveUp(tr("已取消。"), tr("已取消。"));
        return;
    }
    if (startFailed) {
        // 真因在 errorString 里（路径失效、权限、被安全软件拦下都有可能），
        // 必须原样带给用户 —— 否则「无法启动」这种提示等于什么都没说。
        giveUp(tr("无法启动浏览器：%1").arg(m_process->errorString()), tr("无法启动浏览器。"));
        return;
    }
    if (timedOut) {
        giveUp(tr("渲染超时：抖音页面没能在 60 秒内就绪。"), tr("抖音页面渲染超时。"));
        return;
    }
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        giveUp(tr("浏览器渲染失败（退出码 %1）。").arg(exitCode), tr("抖音页面渲染失败。"));
        return;
    }

    const QString dom = QString::fromUtf8(m_dom);
    const QString videoId = videodl::parseDouyinVideoId(dom);
    const QString title = videodl::parseDouyinTitle(dom);

    if (videoId.isEmpty()) {
        giveUp(tr("页面渲染完了，但里面没有 video_id —— 多半是抖音又改版了。"),
               tr("没能从抖音页面里取到播放地址。"));
        return;
    }

    emit logLine(tr("已取到 video_id：%1").arg(videoId));
    if (!title.isEmpty()) {
        emit logLine(tr("标题：%1").arg(title));
    }

    const QString playUrl =
        QStringLiteral("https://www.douyin.com/aweme/v1/play/?video_id=%1&ratio=%2&line=0")
            .arg(videoId, videodl::douyinRatio(m_quality));
    emit logLine(tr("播放地址接口：%1").arg(playUrl));
    emit resolved(playUrl, title);
}
