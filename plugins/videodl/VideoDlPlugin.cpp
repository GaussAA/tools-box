#include "VideoDlPlugin.h"

#include "EngineFetcher.h"
#include "core/CookieFile.h"
#include "core/DouyinSupport.h"
#include "core/EngineLocator.h"
#include "core/OutputParsing.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// 内核的下载源。「下什么、下到哪」是调用方的策略，所以留在页面这一侧，
// EngineFetcher 只管「怎么下」。
const QString kYtDlpDownloadUrl =
    QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe");
// 用 GitHub 上的构建，而不是 gyan.dev：gyan.dev 在国内多数网络下几乎拉不动
// （实测 10 分钟只下来 8MB 且随后彻底停住），GitHub 这边能跑到 3MB/s。
const QString kFfmpegDownloadUrl = QStringLiteral(
    "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip");

/// 适配器：内核目录的根取当前程序目录。
///
/// 真正的规则在 core/EngineLocator.h（这样才测得了文件布局，见
/// docs/architecture.md §3）。这里只是把 QCoreApplication 接上去，
/// 免得下面几十个调用点都要写一遍 applicationDirPath()。
QString engineDir()
{
    return videodl::engineDir(QCoreApplication::applicationDirPath());
}

/// 找一个能用来做无头渲染的浏览器。
///
/// 抖音的播放直链接口要一个 video_id，而这个 id 只有等抖音自己的页面脚本
/// 跑完才会出现在 DOM 里。yt-dlp 走的是需要签名的接口（它的源码里那行 TODO
/// 说明签名压根没实现），所以只能借系统浏览器把页面渲染一遍，再回头解析。
/// 返回空串表示这台机器上没找到可用的浏览器。
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

} // namespace

/// 工具页面：视频下载。
///
/// 除了界面，它还实现了 toolbox::IToolPage —— 保存目录、画质选择以及
/// 手动指定的内核路径都由外壳自动存取，插件不碰 QSettings 键名。
class VideoDlPage : public QWidget, public toolbox::IToolPage
{
    Q_OBJECT
    Q_INTERFACES(toolbox::IToolPage)

public:
    explicit VideoDlPage(QWidget *parent = nullptr);
    ~VideoDlPage() override;

    void restoreState(const toolbox::ToolSettings &settings) override;
    void saveState(const toolbox::ToolSettings &settings) override;

private:
    void buildUi();
    void appendLog(const QString &line);
    void setStatus(const QString &text);
    /// 进度条：percent 为负时切到不确定态（没有百分比可报的阶段）。
    void setProgress(int percent);

    void refreshEngineStatus();
    void updateBusyState();
    QString resolvedYtDlp() const;
    QString resolvedFfmpeg() const;

    void pickYtDlp();
    void pickFfmpeg();
    void pickCookies();
    void fetchYtDlp();
    void fetchFfmpeg();
    /// 内核下载结束（失败或成功都走这里；取消由按钮那边自己收尾）。
    void onFetcherFinished(bool ok, EngineFetcher::Kind kind, const QString &targetPath);

    void startDownload();
    void cancelDownload();
    void onProcessOutput();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void handleOutputLine(const QString &line);

    /// 抖音专用的两段式下载：先用浏览器无头渲染页面取 video_id，
    /// 再拿播放直链接口去下。返回 true 表示已经接管这次下载。
    bool beginDouyinDownload(const QString &url, const QString &dir, int quality, bool audioOnly);
    void onRenderOutput();
    void onRenderFinished(int exitCode, QProcess::ExitStatus status);
    void launchDownload(const QString &target, const QString &titleHint, const QString &dir,
                        int quality, bool audioOnly, bool needsReferer);

    // 输入区
    QLineEdit *m_url = nullptr;
    QLineEdit *m_dir = nullptr;
    QComboBox *m_quality = nullptr;
    QPushButton *m_browseDir = nullptr;
    QPushButton *m_openDir = nullptr;
    QLineEdit *m_cookies = nullptr;
    QPushButton *m_pickCookies = nullptr;
    QPushButton *m_download = nullptr;
    QPushButton *m_cancel = nullptr;
    QLabel *m_status = nullptr;
    QProgressBar *m_progress = nullptr;

    // 内核区
    QLabel *m_engineStatus = nullptr;
    QPushButton *m_fetchYtDlp = nullptr;
    QPushButton *m_fetchFfmpeg = nullptr;
    QPushButton *m_pickYtDlp = nullptr;
    QPushButton *m_pickFfmpeg = nullptr;

    // 日志
    QPlainTextEdit *m_log = nullptr;

    QProcess *m_process = nullptr;
    EngineFetcher *m_fetcher = nullptr; ///< 内核下载（yt-dlp / ffmpeg），见 EngineFetcher.h

    // 抖音解析用的无头浏览器（只在抖音地址上才会启动）
    QProcess *m_render = nullptr;
    QByteArray m_renderOut;   ///< 浏览器吐出来的整份 DOM
    QString m_renderDir;      ///< 本次下载的保存目录（渲染完接着用）
    int m_renderQuality = 0;
    bool m_renderAudioOnly = false;
    bool m_renderTimedOut = false;
    bool m_renderStartFailed = false; ///< 浏览器压根没起来（与「起来了但超时」是两回事）
    bool m_renderCancelled = false;
    int m_renderGeneration = 0; ///< 轮次编号，用来让过期的超时定时器失效

    // 手工指定的路径（留空则回退到随程序目录与 PATH）
    QString m_ytDlpManual;
    QString m_ffmpegManual;

    QByteArray m_pending;   ///< 尚未凑成整行的程序输出
    QString m_lastOutput;   ///< 最近一次识别到的最终文件路径
};

VideoDlPage::VideoDlPage(QWidget *parent)
    : QWidget(parent)
    , m_process(new QProcess(this))
    , m_fetcher(new EngineFetcher(this))
    , m_render(new QProcess(this))
{
    buildUi();

    // 内核下载只通过信号回话：日志、进度、阶段、结束。页面不再持有
    // QNetworkAccessManager / 下载用的 QFile（见 docs/architecture.md 计划 D2）。
    connect(m_fetcher, &EngineFetcher::logLine, this, &VideoDlPage::appendLog);
    connect(m_fetcher, &EngineFetcher::progress, this, &VideoDlPage::setProgress);
    connect(m_fetcher, &EngineFetcher::status, this, &VideoDlPage::setStatus);
    connect(m_fetcher, &EngineFetcher::finished, this, &VideoDlPage::onFetcherFinished);

    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &VideoDlPage::onProcessOutput);
    connect(m_process, &QProcess::finished, this, &VideoDlPage::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            appendLog(tr("无法启动下载内核，请检查 yt-dlp 路径是否有效。"));
            m_progress->setRange(0, 100);
            m_progress->setValue(0);
            setStatus(tr("无法启动下载内核。"));
            updateBusyState();
        }
    });

    // 渲染进程要单独收 stdout（整份 DOM 都在那上面），所以不能合并通道。
    m_render->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_render, &QProcess::readyReadStandardOutput, this, &VideoDlPage::onRenderOutput);
    connect(m_render, &QProcess::finished, this, &VideoDlPage::onRenderFinished);
    // 启动失败与「渲染超时」必须分开记：早先这里复用 m_renderTimedOut，于是浏览器
    // 根本没起来时也报「60 秒超时」，把排查往完全错误的方向引（真因在 errorString
    // 里，却因为走了超时分支被丢掉了）。
    connect(m_render, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_renderStartFailed = true;
            onRenderFinished(-1, QProcess::NormalExit);
        }
    });

    refreshEngineStatus();
    updateBusyState();
}

VideoDlPage::~VideoDlPage()
{
    // 页面可能因为重载插件或关窗被销毁，此时进程还在跑就会报
    // "QProcess: Destroyed while process is still running"，先收干净。
    for (QProcess *p : {m_process, m_render}) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
    QDir(QDir::tempPath() + QStringLiteral("/toolbox-douyin-render")).removeRecursively();
    QFile::remove(videodl::normalizedCookiesPath());
}

void VideoDlPage::buildUi()
{
    auto *layout = new QVBoxLayout(this);

    layout->addWidget(new QLabel(
        tr("粘贴视频地址（支持 B站 / YouTube / 抖音 等站点），选好画质后点「开始下载」。"), this));

    m_url = new QLineEdit(this);
    m_url->setPlaceholderText(tr("https://…（视频页面地址）"));
    m_url->setClearButtonEnabled(true);

    m_dir = new QLineEdit(this);
    m_browseDir = new QPushButton(tr("浏览…"), this);
    m_openDir = new QPushButton(tr("打开保存目录"), this);

    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(new QLabel(tr("保存到"), this));
    dirRow->addWidget(m_dir, 1);
    dirRow->addWidget(m_browseDir);
    dirRow->addWidget(m_openDir);

    m_quality = new QComboBox(this);
    m_quality->addItem(tr("最高画质（自动合并为 mp4）"));
    m_quality->addItem(tr("1080p 及以下"));
    m_quality->addItem(tr("720p 及以下"));
    m_quality->addItem(tr("480p 及以下"));
    m_quality->addItem(tr("仅音频（mp3）"));

    auto *qualityRow = new QHBoxLayout;
    qualityRow->addWidget(new QLabel(tr("画质"), this));
    qualityRow->addWidget(m_quality, 1);

    // 有些内容（B站 会员视频、YouTube 年龄限制）要带着浏览器 cookies 才给看。
    // 注意 yt-dlp 读不了 Chrome/Edge 的 cookie 库 —— 两家都启用了 App-Bound
    // Encryption，密钥由浏览器的提权服务持有，第三方进程解不开，只能让用户
    // 用扩展导出成文件传进来。（抖音不走这条路，见 beginDouyinDownload。）
    m_cookies = new QLineEdit(this);
    m_cookies->setPlaceholderText(tr("可选：浏览器导出的 cookies.txt（B站 会员内容等需要）"));
    m_cookies->setClearButtonEnabled(true);
    m_pickCookies = new QPushButton(tr("浏览…"), this);

    auto *cookieRow = new QHBoxLayout;
    cookieRow->addWidget(new QLabel(tr("Cookie 文件"), this));
    cookieRow->addWidget(m_cookies, 1);
    cookieRow->addWidget(m_pickCookies);

    layout->addWidget(new QLabel(tr("视频地址"), this));
    layout->addWidget(m_url);
    layout->addLayout(dirRow);
    layout->addLayout(qualityRow);
    layout->addLayout(cookieRow);

    // ── 下载内核 ──────────────────────────────────────────────
    auto *engineBox = new QGroupBox(tr("下载内核（yt-dlp / ffmpeg）"), this);
    auto *engineLayout = new QVBoxLayout(engineBox);

    m_engineStatus = new QLabel(engineBox);
    m_engineStatus->setWordWrap(true);
    m_engineStatus->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_fetchYtDlp = new QPushButton(tr("下载 / 更新 yt-dlp"), engineBox);
    m_fetchFfmpeg = new QPushButton(tr("下载 ffmpeg"), engineBox);
    m_pickYtDlp = new QPushButton(tr("手动指定 yt-dlp…"), engineBox);
    m_pickFfmpeg = new QPushButton(tr("手动指定 ffmpeg…"), engineBox);

    auto *engineButtons = new QHBoxLayout;
    engineButtons->addWidget(m_fetchYtDlp);
    engineButtons->addWidget(m_fetchFfmpeg);
    engineButtons->addWidget(m_pickYtDlp);
    engineButtons->addWidget(m_pickFfmpeg);
    engineButtons->addStretch(1);

    engineLayout->addWidget(m_engineStatus);
    engineLayout->addLayout(engineButtons);
    layout->addWidget(engineBox);

    // ── 操作与进度 ────────────────────────────────────────────
    m_download = new QPushButton(tr("开始下载"), this);
    m_cancel = new QPushButton(tr("取消"), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);

    // 进度条只会给出百分比，「现在到底在干什么」得靠这行文字说清楚：
    // 解析地址、下载中（带速度/剩余）、合并、转码、解压内核都走它。
    m_status = new QLabel(tr("就绪。"), this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *actionRow = new QHBoxLayout;
    actionRow->addWidget(m_download);
    actionRow->addWidget(m_cancel);
    actionRow->addStretch(1);

    layout->addLayout(actionRow);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);

    // ── 日志 ──────────────────────────────────────────────────
    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    m_log->setPlaceholderText(tr("下载日志会显示在这里"));
    // 长任务会刷很多行，限制下落避免无限增长。
    m_log->setMaximumBlockCount(5000);
    layout->addWidget(new QLabel(tr("日志"), this));
    layout->addWidget(m_log, 1);

    connect(m_browseDir, &QPushButton::clicked, this, [this] {
        const QString dir =
            QFileDialog::getExistingDirectory(this, tr("选择保存目录"), m_dir->text());
        if (!dir.isEmpty()) {
            m_dir->setText(QDir::toNativeSeparators(dir));
        }
    });
    connect(m_openDir, &QPushButton::clicked, this, [this] {
        const QString dir = m_dir->text().trimmed();
        if (dir.isEmpty()) {
            return;
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    });

    connect(m_download, &QPushButton::clicked, this, &VideoDlPage::startDownload);
    connect(m_cancel, &QPushButton::clicked, this, &VideoDlPage::cancelDownload);

    connect(m_fetchYtDlp, &QPushButton::clicked, this, &VideoDlPage::fetchYtDlp);
    connect(m_fetchFfmpeg, &QPushButton::clicked, this, &VideoDlPage::fetchFfmpeg);
    connect(m_pickYtDlp, &QPushButton::clicked, this, &VideoDlPage::pickYtDlp);
    connect(m_pickFfmpeg, &QPushButton::clicked, this, &VideoDlPage::pickFfmpeg);
    connect(m_pickCookies, &QPushButton::clicked, this, &VideoDlPage::pickCookies);
}

void VideoDlPage::appendLog(const QString &line)
{
    m_log->appendPlainText(line);
}

void VideoDlPage::setStatus(const QString &text)
{
    m_status->setText(text);
}

QString VideoDlPage::resolvedYtDlp() const
{
    return videodl::resolveExecutable(m_ytDlpManual, QStringLiteral("yt-dlp.exe"), engineDir());
}

QString VideoDlPage::resolvedFfmpeg() const
{
    return videodl::resolveExecutable(m_ffmpegManual, QStringLiteral("ffmpeg.exe"), engineDir());
}

void VideoDlPage::refreshEngineStatus()
{
    const QString ytDlp = resolvedYtDlp();
    const QString ffmpeg = resolvedFfmpeg();

    const QString ytDlpText = ytDlp.isEmpty() ? tr("未找到 —— 点下面的按钮下载，或手动指定路径")
                                              : QDir::toNativeSeparators(ytDlp);
    const QString ffmpegText = ffmpeg.isEmpty() ? tr("未找到 —— 高画质合并与「仅音频」将不可用")
                                                : QDir::toNativeSeparators(ffmpeg);

    m_engineStatus->setText(tr("yt-dlp：%1\nffmpeg：%2").arg(ytDlpText, ffmpegText));

    updateBusyState();
}

void VideoDlPage::updateBusyState()
{
    const bool videoRunning = m_process->state() != QProcess::NotRunning;
    const bool rendering = m_render->state() != QProcess::NotRunning;
    const bool fetching = m_fetcher->isBusy();
    const bool busy = videoRunning || rendering || fetching;

    m_download->setEnabled(!busy && !resolvedYtDlp().isEmpty());
    // 内核下载同样要能取消：ffmpeg 那个包有 190MB，中途想停却没有入口、
    // 只能关窗口，太粗暴（取消与失败还会在日志里混成一团）。
    m_cancel->setEnabled(videoRunning || rendering || fetching);

    m_fetchYtDlp->setEnabled(!busy);
    m_fetchFfmpeg->setEnabled(!busy);
    m_pickYtDlp->setEnabled(!busy);
    m_pickFfmpeg->setEnabled(!busy);
    m_browseDir->setEnabled(!busy);
    m_pickCookies->setEnabled(!busy);
}

// ── 内核下载 ──────────────────────────────────────────────────

void VideoDlPage::fetchYtDlp()
{
    if (!QDir().mkpath(engineDir())) {
        appendLog(tr("无法创建目录：%1").arg(QDir::toNativeSeparators(engineDir())));
        setStatus(tr("无法创建内核目录：%1").arg(QDir::toNativeSeparators(engineDir())));
        return;
    }
    m_fetcher->start(EngineFetcher::Kind::YtDlp, kYtDlpDownloadUrl,
                     engineDir() + QStringLiteral("/yt-dlp.exe"));
    updateBusyState();
}

void VideoDlPage::fetchFfmpeg()
{
    // ffmpeg 官方只发 zip，先落到临时目录，解压后再把 ffmpeg.exe 挑出来。
    const QString zipPath = QDir::tempPath() + QStringLiteral("/toolbox-ffmpeg.zip");
    m_fetcher->start(EngineFetcher::Kind::Ffmpeg, kFfmpegDownloadUrl, zipPath);
    updateBusyState();
}

void VideoDlPage::setProgress(int percent)
{
    if (percent < 0) {
        // 不确定态：解析地址、解压这类阶段没有百分比可报。
        m_progress->setRange(0, 0);
        return;
    }
    m_progress->setRange(0, 100);
    m_progress->setValue(percent);
}

void VideoDlPage::onFetcherFinished(bool ok, EngineFetcher::Kind kind, const QString &targetPath)
{
    Q_UNUSED(ok)
    Q_UNUSED(kind)
    Q_UNUSED(targetPath)
    // 日志与状态文案由 EngineFetcher 自己发出；这里只需按新的内核状态刷新按钮。
    refreshEngineStatus();
}

void VideoDlPage::pickYtDlp()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("选择 yt-dlp 可执行文件"), QString(),
                                                      tr("可执行文件 (*.exe);;所有文件 (*)"));
    if (file.isEmpty()) {
        return;
    }
    m_ytDlpManual = file;
    appendLog(tr("已指定 yt-dlp：%1").arg(QDir::toNativeSeparators(file)));
    refreshEngineStatus();
}

void VideoDlPage::pickFfmpeg()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("选择 ffmpeg 可执行文件"), QString(),
                                                      tr("可执行文件 (*.exe);;所有文件 (*)"));
    if (file.isEmpty()) {
        return;
    }
    m_ffmpegManual = file;
    appendLog(tr("已指定 ffmpeg：%1").arg(QDir::toNativeSeparators(file)));
    refreshEngineStatus();
}

void VideoDlPage::pickCookies()
{
    const QString file = QFileDialog::getOpenFileName(this, tr("选择 cookies.txt"), QString(),
                                                      tr("Cookie 文件 (*.txt);;所有文件 (*)"));
    if (file.isEmpty()) {
        return;
    }
    m_cookies->setText(QDir::toNativeSeparators(file));
    appendLog(tr("已指定 cookies 文件：%1").arg(QDir::toNativeSeparators(file)));
}

// ── 视频下载 ──────────────────────────────────────────────────

void VideoDlPage::startDownload()
{
    const QString rawInput = m_url->text().trimmed();
    if (rawInput.isEmpty()) {
        QMessageBox::information(this, tr("缺少地址"), tr("请先粘贴视频地址。"));
        return;
    }

    // 从 App 里点「复制链接」拿到的常常是一整段分享文案：表情、话题标签、
    // 口令和说明文字全都混在一起，真正的地址只是其中一小段。抠地址的规则
    // 在 core/OutputParsing.h，用户直接整段粘贴也能用。
    const QString url = videodl::extractUrl(rawInput);

    const QString ytDlp = resolvedYtDlp();
    if (ytDlp.isEmpty()) {
        QMessageBox::warning(this, tr("缺少下载内核"),
                             tr("还没有可用的 yt-dlp。请点「下载 / 更新 yt-dlp」，"
                                "或手动指定一个 yt-dlp.exe 路径。"));
        return;
    }

    const QString dir = m_dir->text().trimmed();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        QMessageBox::warning(this, tr("保存目录无效"), tr("请选择一个可写入的保存目录。"));
        return;
    }

    const QString ffmpeg = resolvedFfmpeg();
    const int quality = m_quality->currentIndex();
    const bool audioOnly = (quality == 4);

    if (audioOnly && ffmpeg.isEmpty()) {
        QMessageBox::warning(this, tr("缺少 ffmpeg"),
                             tr("「仅音频」需要用 ffmpeg 转成 mp3。"
                                "请先点「下载 ffmpeg」，或改选其它画质。"));
        return;
    }

    m_log->clear();
    if (url != rawInput) {
        appendLog(tr("已从粘贴内容中识别出地址：%1").arg(url));
    }

    // 抖音要走「浏览器渲染 + 播放直链接口」那条路，交给它自己接管。
    if (beginDouyinDownload(url, dir, quality, audioOnly)) {
        return;
    }

    launchDownload(url, QString(), dir, quality, audioOnly, false);
}

void VideoDlPage::launchDownload(const QString &target, const QString &titleHint,
                                 const QString &dir, int quality, bool audioOnly, bool needsReferer)
{
    const QString ytDlp = resolvedYtDlp();
    const QString ffmpeg = resolvedFfmpeg();

    QStringList args;
    args << QStringLiteral("--newline") // 让进度按行刷新，才好解析
         << QStringLiteral("--no-playlist") // 只下载当前这一个视频
         << QStringLiteral("-P") << dir;

    const QString defaultTemplate = QStringLiteral("%(title)s [%(id)s].%(ext)s");
    if (titleHint.isEmpty()) {
        args << QStringLiteral("-o") << defaultTemplate;
    } else {
        // 抖音那条路拿到的是直链，generic extractor 只会把文件叫成 video.mp4，
        // 只好把页面标题直接写进输出模板。
        //
        // 标题取自抖音页面的 DOM，是**外部输入**：里面一个 / 或 \ 就会被 yt-dlp
        // 当成路径分隔符，把文件写到保存目录之外。先按文件名规则消毒，
        // 消毒后为空说明这个标题救不回来，回落默认模板而不是拿空名字去下载。
        const QString safeTitle = videodl::sanitizeFileName(titleHint);
        if (safeTitle.isEmpty()) {
            appendLog(tr("视频标题无法用作文件名，已改用默认命名。"));
            args << QStringLiteral("-o") << defaultTemplate;
        } else {
            // 百分号在模板里有含义，先转义掉。
            const QString escaped =
                QString(safeTitle).replace(QLatin1Char('%'), QStringLiteral("%%"));
            args << QStringLiteral("-o") << (escaped + QStringLiteral(".%(ext)s"));
        }
    }

    if (needsReferer) {
        // 抖音的 CDN 会检查来源，少这个头就会 403。
        args << QStringLiteral("--referer") << QStringLiteral("https://www.douyin.com/");
    }

    // 部分 B站 会员内容、YouTube 年龄限制内容需要浏览器 cookies。
    const QString cookies = m_cookies->text().trimmed();
    if (!cookies.isEmpty() && QFileInfo::exists(cookies)) {
        // 导出扩展产出的文件常常不合规，直接喂给 yt-dlp 会被整份拒收，
        // 先收拾一份干净的副本出来（详见 core/CookieFile.h 的注释）。
        int fixedRows = 0;
        int droppedRows = 0;
        const QString normalized = videodl::normalizeCookies(
            cookies, videodl::normalizedCookiesPath(), &fixedRows, &droppedRows);
        if (normalized.isEmpty()) {
            appendLog(tr("cookies 文件读不出来，本次下载不使用它：%1")
                          .arg(QDir::toNativeSeparators(cookies)));
        } else {
            args << QStringLiteral("--cookies") << normalized;
            if (fixedRows > 0 || droppedRows > 0) {
                appendLog(tr("cookies 文件格式不合规，已自动修正 %1 行、丢弃 %2 行畸形记录。")
                              .arg(fixedRows)
                              .arg(droppedRows));
            }
        }
    }

    if (audioOnly) {
        args << QStringLiteral("-x") << QStringLiteral("--audio-format") << QStringLiteral("mp3");
    } else if (ffmpeg.isEmpty()) {
        // 没有 ffmpeg 就没法合并音视频分轨，退回「最佳单文件」。
        // 代价是清晰度通常只有 360p/480p，日志里会说明。
        args << QStringLiteral("-f") << QStringLiteral("b");
    } else {
        args << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
        switch (quality) {
        case 1:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]");
            break;
        case 2:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=720]+ba/b[height<=720]");
            break;
        case 3:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=480]+ba/b[height<=480]");
            break;
        default:
            args << QStringLiteral("-f") << QStringLiteral("bv*+ba/b");
            break;
        }
    }

    if (!ffmpeg.isEmpty()) {
        // 只传目录即可，yt-dlp 会自己去里面找 ffmpeg。
        args << QStringLiteral("--ffmpeg-location") << QFileInfo(ffmpeg).absolutePath();
    }

    args << target;

    m_lastOutput.clear();
    m_pending.clear();
    // 解析地址这一步 yt-dlp 不吐百分比，进度条先走不确定态，
    // 等第一行 [download] xx% 出来再切回确定态。
    m_progress->setRange(0, 0);
    setStatus(tr("正在解析视频信息…"));
    // 命令行里带着 cookies 副本的路径与完整视频地址，原样打进日志等于把「凭据在哪」
    // 写给每一个看得到这段日志的人（截图、问题反馈里贴的往往就是这一行）。
    appendLog(tr("执行：%1").arg(videodl::redactCommand(QDir::toNativeSeparators(ytDlp), args)));

    if (ffmpeg.isEmpty()) {
        appendLog(tr("提示：未检测到 ffmpeg，已降级为单文件下载，清晰度可能受限。"));
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    m_process->setProcessEnvironment(env);

    m_process->start(ytDlp, args);
    updateBusyState();
}

// ── 抖音：借浏览器渲染取 video_id ─────────────────────────────
//
// yt-dlp 的抖音实现直接打需要签名的 web detail 接口（它源码里那行
// 「TODO: Run verification challenge code to generate signature cookies」
// 说明签名一直没做），抖音收紧校验后必然 403，换 cookies 也治不好。
// 换成：让系统浏览器把页面正常跑一遍（JS 该执行的都执行了），
// 再从渲染结果里取 video_id，用公开的 aweme/v1/play 接口换到直链。
bool VideoDlPage::beginDouyinDownload(const QString &url, const QString &dir, int quality,
                                      bool audioOnly)
{
    if (!videodl::isDouyinUrl(url)) {
        return false;
    }

    const QString browser = findHeadlessBrowser();
    if (browser.isEmpty()) {
        appendLog(tr("抖音要靠浏览器渲染页面才能取到播放地址，但这台机器上没找到 "
                     "Edge 或 Chrome。"));
        setStatus(tr("未找到可用的浏览器。"));
        return true; // 已经接管这次下载，只是失败了
    }

    m_renderDir = dir;
    m_renderQuality = quality;
    m_renderAudioOnly = audioOnly;
    m_renderOut.clear();
    m_renderTimedOut = false;
    m_renderStartFailed = false;
    m_renderCancelled = false;

    // 用独立的 profile 目录，免得去碰用户正在用的浏览器数据。
    const QString profileDir = QDir::tempPath() + QStringLiteral("/toolbox-douyin-render");
    QDir(profileDir).removeRecursively();

    const QStringList args{
        QStringLiteral("--headless=new"),
        QStringLiteral("--disable-gpu"),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
        // 无头模式默认会在 navigator.webdriver 上暴露自己，抖音认这个，关掉。
        QStringLiteral("--disable-blink-features=AutomationControlled"),
        QStringLiteral("--user-data-dir=") + profileDir,
        // 抖音的脚本要跑十几秒才会把 video_id 写进 DOM，虚拟时间给足。
        QStringLiteral("--virtual-time-budget=20000"),
        QStringLiteral("--dump-dom"),
        url,
    };

    appendLog(tr("抖音的播放地址要等页面脚本跑完才出现，正在用浏览器渲染…"));
    appendLog(tr("渲染器：%1").arg(QDir::toNativeSeparators(browser)));
    m_progress->setRange(0, 0);
    setStatus(tr("正在渲染抖音页面…"));

    m_render->start(browser, args);

    // 这里刻意**不** waitForStarted()：一来它会在 GUI 线程上阻塞最多 5 秒，二来
    // start() 失败时「waitForStarted 返回 false」与「errorOccurred(FailedToStart)」
    // 两条路径都会说话，日志里出现两条互相矛盾的失败原因。启动结果统一交给
    // onRenderFinished 一处判定。
    //
    // 超时定时器因此从「启动」就开始计时：正常十几秒跑完，60 秒还没动静就是卡住了。
    // 带上次序号，免得上一次留下的定时器把这一轮刚启动的进程杀掉。
    const int generation = ++m_renderGeneration;
    QTimer::singleShot(60000, this, [this, generation] {
        if (generation == m_renderGeneration && m_render->state() != QProcess::NotRunning) {
            m_renderTimedOut = true;
            m_render->kill();
        }
    });

    updateBusyState();
    return true;
}

void VideoDlPage::onRenderOutput()
{
    m_renderOut += m_render->readAllStandardOutput();
}

void VideoDlPage::onRenderFinished(int exitCode, QProcess::ExitStatus status)
{
    m_renderOut += m_render->readAllStandardOutput(); // 收尾，别漏掉最后一段

    const bool cancelled = m_renderCancelled;
    const bool timedOut = m_renderTimedOut;
    const bool startFailed = m_renderStartFailed;
    m_renderCancelled = false;
    m_renderTimedOut = false;
    m_renderStartFailed = false;

    // 浏览器会在 profile 目录里堆一堆文件，用完就清干净。
    QDir(QDir::tempPath() + QStringLiteral("/toolbox-douyin-render")).removeRecursively();

    const auto giveUp = [this](const QString &logLine, const QString &statusLine) {
        appendLog(logLine);
        setStatus(statusLine);
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        updateBusyState();
    };

    if (cancelled) {
        giveUp(tr("已取消。"), tr("已取消。"));
        return;
    }
    if (startFailed) {
        // 真因在 errorString 里（路径失效、权限、被安全软件拦下都有可能），
        // 必须原样带给用户 —— 否则「无法启动」这种提示等于什么都没说。
        giveUp(tr("无法启动浏览器：%1").arg(m_render->errorString()), tr("无法启动浏览器。"));
        return;
    }
    if (timedOut) {
        giveUp(tr("渲染超时：抖音页面没能在 60 秒内就绪。"), tr("抖音页面渲染超时。"));
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        giveUp(tr("浏览器渲染失败（退出码 %1）。").arg(exitCode), tr("抖音页面渲染失败。"));
        return;
    }

    const QString dom = QString::fromUtf8(m_renderOut);
    const QString videoId = videodl::parseDouyinVideoId(dom);
    const QString title = videodl::parseDouyinTitle(dom);

    if (videoId.isEmpty()) {
        giveUp(tr("页面渲染完了，但里面没有 video_id —— 多半是抖音又改版了。"),
               tr("没能从抖音页面里取到播放地址。"));
        return;
    }

    appendLog(tr("已取到 video_id：%1").arg(videoId));
    if (!title.isEmpty()) {
        appendLog(tr("标题：%1").arg(title));
    }

    const QString playUrl =
        QStringLiteral("https://www.douyin.com/aweme/v1/play/?video_id=%1&ratio=%2&line=0")
            .arg(videoId, videodl::douyinRatio(m_renderQuality));
    appendLog(tr("播放地址接口：%1").arg(playUrl));

    launchDownload(playUrl, title, m_renderDir, m_renderQuality, m_renderAudioOnly, true);
}

void VideoDlPage::cancelDownload()
{
    if (m_fetcher->isBusy()) {
        // 取消不是失败：EngineFetcher 那边不会重试，也不会报「下载失败」。
        appendLog(tr("已取消内核下载。"));
        setStatus(tr("已取消内核下载。"));
        m_fetcher->cancel();
        updateBusyState();
        return;
    }

    // 抖音那条路在渲染阶段就点取消：浏览器进程也要一起收掉。
    if (m_render->state() != QProcess::NotRunning) {
        appendLog(tr("正在取消…"));
        setStatus(tr("正在取消…"));
        m_renderCancelled = true;
        m_render->kill();
        return;
    }
    if (m_process->state() == QProcess::NotRunning) {
        return;
    }
    appendLog(tr("正在取消…（已下载的临时文件可能残留在保存目录）"));
    setStatus(tr("正在取消…"));
    m_process->kill();
}

void VideoDlPage::onProcessOutput()
{
    m_pending += m_process->readAllStandardOutput();

    int newline = -1;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        QByteArray raw = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        if (raw.endsWith('\r')) {
            raw.chop(1);
        }
        handleOutputLine(videodl::stripAnsi(videodl::decodeOutput(raw)));
    }
}

void VideoDlPage::handleOutputLine(const QString &line)
{
    if (line.trimmed().isEmpty()) {
        return;
    }
    appendLog(line);

    // 解析规则都在 videodl_core 里（见 core/OutputParsing.h），这里只负责把
    // 解析结果翻译成界面上的进度条与状态文案 —— 文案要 tr()，属于 View。
    const videodl::ProgressInfo progress = videodl::parseProgress(line);
    if (progress.matched) {
        m_progress->setRange(0, 100);
        m_progress->setValue(progress.percent);

        const QString hint = progress.eta.isEmpty()
            ? progress.speed
            : tr("%1，剩余 %2").arg(progress.speed, progress.eta);
        setStatus(hint.isEmpty() ? tr("正在下载… %1%").arg(progress.percent)
                                 : tr("正在下载… %1%（%2）").arg(progress.percent).arg(hint));
    } else {
        switch (videodl::classifyStage(line)) {
        case videodl::OutputStage::DownloadStarting:
            setStatus(tr("正在下载…"));
            break;
        case videodl::OutputStage::Merging:
            // 合并音视频分轨要花十几秒到几分钟，这期间没有任何百分比可报，
            // 不切不确定态的话进度条会一直停在 100%，看着像卡死。
            m_progress->setRange(0, 0);
            setStatus(tr("正在合并音视频…"));
            break;
        case videodl::OutputStage::ExtractingAudio:
            m_progress->setRange(0, 0);
            setStatus(tr("正在提取音频…"));
            break;
        case videodl::OutputStage::None:
            break;
        }
    }

    // 记下最终产物：普通下载给 Destination，合并/转码后给的是另一条。
    const QString destination = videodl::parseDestination(line);
    if (!destination.isEmpty()) {
        m_lastOutput = destination;
    }
}

void VideoDlPage::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // 规范化出来的那份 cookies 副本里有登录凭据，任务一结束就删掉，
    // 别让它在临时目录里躺着。
    QFile::remove(videodl::normalizedCookiesPath());

    // 收尾：把缓冲区里最后没带换行的一行也处理掉。
    if (!m_pending.isEmpty()) {
        handleOutputLine(videodl::stripAnsi(videodl::decodeOutput(m_pending)));
        m_pending.clear();
    }

    if (status == QProcess::CrashExit) {
        appendLog(tr("下载已取消或进程异常退出。"));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        setStatus(tr("已取消。"));
    } else if (exitCode == 0) {
        m_progress->setRange(0, 100);
        m_progress->setValue(100);
        if (m_lastOutput.isEmpty()) {
            appendLog(tr("下载完成。"));
            setStatus(tr("下载完成。"));
        } else {
            appendLog(tr("下载完成：%1").arg(QDir::toNativeSeparators(m_lastOutput)));
            // 状态文字里只放文件名：整条路径通常很长，会把这一行撑得很难看。
            setStatus(tr("下载完成：%1").arg(QFileInfo(m_lastOutput).fileName()));
        }
    } else {
        appendLog(tr("下载失败（退出码 %1）。").arg(exitCode));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        setStatus(tr("下载失败（退出码 %1）。").arg(exitCode));
    }

    updateBusyState();
}

// ── 配置持久化 ────────────────────────────────────────────────

void VideoDlPage::restoreState(const toolbox::ToolSettings &settings)
{
    QString dir = settings.value(QStringLiteral("saveDir")).toString();
    if (dir.isEmpty()) {
        dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
        if (dir.isEmpty()) {
            dir = QDir::homePath();
        }
    }
    m_dir->setText(QDir::toNativeSeparators(dir));

    m_quality->setCurrentIndex(
        qBound(0, settings.value(QStringLiteral("quality"), 0).toInt(), m_quality->count() - 1));

    m_ytDlpManual = settings.value(QStringLiteral("ytDlpPath")).toString();
    m_ffmpegManual = settings.value(QStringLiteral("ffmpegPath")).toString();
    m_cookies->setText(settings.value(QStringLiteral("cookiesPath")).toString());

    refreshEngineStatus();
}

void VideoDlPage::saveState(const toolbox::ToolSettings &settings)
{
    settings.setValue(QStringLiteral("saveDir"), m_dir->text());
    settings.setValue(QStringLiteral("quality"), m_quality->currentIndex());
    settings.setValue(QStringLiteral("ytDlpPath"), m_ytDlpManual);
    settings.setValue(QStringLiteral("ffmpegPath"), m_ffmpegManual);
    settings.setValue(QStringLiteral("cookiesPath"), m_cookies->text());
}

// ── 插件入口 ──────────────────────────────────────────────────

toolbox::ToolMeta VideoDlPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("media.video-download");
    info.name = tr("视频下载");
    info.category = tr("媒体工具");
    info.version = QStringLiteral("0.1.0");
    info.description =
        tr("粘贴 B站 / YouTube / 抖音 等视频地址，按画质下载到本地（内核为 yt-dlp）。");
    info.icon = QIcon(QStringLiteral(":/icons/videodl.svg"));
    return info;
}

QWidget *VideoDlPlugin::createPage(QWidget *parent)
{
    return new VideoDlPage(parent);
}

#include "VideoDlPlugin.moc"
