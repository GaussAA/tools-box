#include "VideoDlPlugin.h"

#include "DouyinResolver.h"
#include "DownloadRunner.h"
#include "EngineFetcher.h"
#include "core/CookieFile.h"
#include "core/DouyinSupport.h"
#include "core/DownloadArgs.h"
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
    /// 按 m_spec 真正发起一次下载（参数由 core 的 buildYtDlpArgs 构造，可单测）。
    void runDownload(const QString &target, const QString &titleHint);
    void onDownloadFinished(int exitCode, bool crashed, const QString &outputPath);
    /// 抖音渲染出直链之后的接力：拿直链再走一次普通下载。
    void onDouyinResolved(const QString &playUrl, const QString &title);

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

    /// 下载本体（yt-dlp 进程），见 DownloadRunner.h。
    DownloadRunner *m_runner = nullptr;
    /// 抖音专线：借浏览器渲染取直链，见 DouyinResolver.h。
    DouyinResolver *m_resolver = nullptr;
    EngineFetcher *m_fetcher = nullptr; ///< 内核下载（yt-dlp / ffmpeg），见 EngineFetcher.h

    // 手工指定的路径（留空则回退到随程序目录与 PATH）
    QString m_ytDlpManual;
    QString m_ffmpegManual;

    /// 本次下载的输入。抖音那条路要分两步：先渲染拿直链，再拿这份参数接着下，
    /// 所以参数得跨两次调用存着。
    videodl::DownloadSpec m_spec;
};

VideoDlPage::VideoDlPage(QWidget *parent)
    : QWidget(parent)
    , m_runner(new DownloadRunner(this))
    , m_resolver(new DouyinResolver(this))
    , m_fetcher(new EngineFetcher(this))
{
    buildUi();

    // 内核下载只通过信号回话：日志、进度、阶段、结束。页面不再持有
    // QNetworkAccessManager / 下载用的 QFile（见 docs/architecture.md 计划 D2）。
    connect(m_fetcher, &EngineFetcher::logLine, this, &VideoDlPage::appendLog);
    connect(m_fetcher, &EngineFetcher::progress, this, &VideoDlPage::setProgress);
    connect(m_fetcher, &EngineFetcher::status, this, &VideoDlPage::setStatus);
    connect(m_fetcher, &EngineFetcher::finished, this, &VideoDlPage::onFetcherFinished);

    connect(m_runner, &DownloadRunner::logLine, this, &VideoDlPage::appendLog);
    connect(m_runner, &DownloadRunner::progress, this, &VideoDlPage::setProgress);
    connect(m_runner, &DownloadRunner::status, this, &VideoDlPage::setStatus);
    connect(m_runner, &DownloadRunner::finished, this, &VideoDlPage::onDownloadFinished);

    connect(m_resolver, &DouyinResolver::logLine, this, &VideoDlPage::appendLog);
    connect(m_resolver, &DouyinResolver::progress, this, &VideoDlPage::setProgress);
    connect(m_resolver, &DouyinResolver::status, this, &VideoDlPage::setStatus);
    connect(m_resolver, &DouyinResolver::resolved, this, &VideoDlPage::onDouyinResolved);
    connect(m_resolver, &DouyinResolver::failed, this, &VideoDlPage::updateBusyState);

    refreshEngineStatus();
    updateBusyState();
}

VideoDlPage::~VideoDlPage()
{
    // 两个子进程由 DownloadRunner / DouyinResolver 在自己的析构里收干净，
    // 这里只需删掉含登录凭据的 cookies 副本。
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
    // 用扩展导出成文件传进来。（抖音不走这条路，见 DouyinResolver.h。）
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
    const bool videoRunning = m_runner->isRunning();
    const bool rendering = m_resolver->isRunning();
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

    // 抖音要走「浏览器渲染 + 播放直链接口」那条路：先渲染拿直链，再由回调接力下载
    // （见 DouyinResolver）。其余站点一次调用就下完。
    videodl::DownloadSpec spec;
    spec.outputDir = dir;
    spec.quality = quality;
    spec.audioOnly = audioOnly;
    spec.hasFfmpeg = !ffmpeg.isEmpty();
    spec.ffmpegDir = ffmpeg.isEmpty() ? QString() : QFileInfo(ffmpeg).absolutePath();
    m_spec = spec;

    if (videodl::isDouyinUrl(url)) {
        m_spec.needsReferer = true; // 抖音的 CDN 会检查来源，少这个头就 403
        m_resolver->start(url, quality);
        updateBusyState();
        return;
    }

    runDownload(url, QString());
}

void VideoDlPage::runDownload(const QString &target, const QString &titleHint)
{
    const QString ytDlp = resolvedYtDlp();
    const QString ffmpeg = resolvedFfmpeg();

    videodl::DownloadSpec spec = m_spec;
    spec.url = target;
    spec.hasFfmpeg = !ffmpeg.isEmpty();
    spec.ffmpegDir = ffmpeg.isEmpty() ? QString() : QFileInfo(ffmpeg).absolutePath();

    // 标题取自抖音页面的 DOM，是**外部输入**：先按文件名规则消毒（sanitizeFileName
    // 会把 / 与 \ 换成下划线，路径穿越就无从谈起），消毒后为空说明这个标题救不回来，
    // 回落默认模板而不是拿空名字去下载。
    if (!titleHint.isEmpty()) {
        const QString safeTitle = videodl::sanitizeFileName(titleHint);
        if (safeTitle.isEmpty()) {
            appendLog(tr("视频标题无法用作文件名，已改用默认命名。"));
        } else {
            spec.title = safeTitle;
        }
    }

    // cookies：浏览器扩展导出的文件常常不合规，直接喂给 yt-dlp 会被整份拒收，
    // 先收拾一份干净的副本出来（详见 core/CookieFile.h 的注释）。副本含登录凭据，
    // 任务结束与页面析构时都会删掉。
    const QString cookies = m_cookies->text().trimmed();
    if (!cookies.isEmpty() && QFileInfo::exists(cookies)) {
        int fixedRows = 0;
        int droppedRows = 0;
        const QString normalized = videodl::normalizeCookies(
            cookies, videodl::normalizedCookiesPath(), &fixedRows, &droppedRows);
        if (normalized.isEmpty()) {
            appendLog(tr("cookies 文件读不出来，本次下载不使用它：%1")
                          .arg(QDir::toNativeSeparators(cookies)));
        } else {
            spec.cookiesPath = normalized;
            if (fixedRows > 0 || droppedRows > 0) {
                appendLog(tr("cookies 文件格式不合规，已自动修正 %1 行、丢弃 %2 行畸形记录。")
                              .arg(fixedRows)
                              .arg(droppedRows));
            }
        }
    }

    const QStringList args = videodl::buildYtDlpArgs(spec);
    // 命令行里带着 cookies 副本的路径与完整视频地址，原样打进日志等于把「凭据在哪」
    // 写给每一个看得到这段日志的人（截图、问题反馈里贴的往往就是这一行）。
    appendLog(tr("执行：%1").arg(videodl::redactCommand(QDir::toNativeSeparators(ytDlp), args)));
    if (ffmpeg.isEmpty()) {
        appendLog(tr("提示：未检测到 ffmpeg，已降级为单文件下载，清晰度可能受限。"));
    }

    // 解析地址这一步 yt-dlp 不吐百分比，进度条先走不确定态，
    // 等第一行 [download] xx% 出来再切回确定态。
    setProgress(-1);
    setStatus(tr("正在解析视频信息…"));
    m_runner->start(ytDlp, args);
    updateBusyState();
}

void VideoDlPage::onDouyinResolved(const QString &playUrl, const QString &title)
{
    runDownload(playUrl, title);
}

void VideoDlPage::onDownloadFinished(int exitCode, bool crashed, const QString &outputPath)
{
    // 规范化出来的那份 cookies 副本里有登录凭据，任务一结束就删掉，
    // 别让它在临时目录里躺着。
    QFile::remove(videodl::normalizedCookiesPath());

    if (crashed) {
        appendLog(tr("下载已取消或进程异常退出。"));
        setProgress(0);
        setStatus(tr("已取消。"));
    } else if (exitCode == 0) {
        setProgress(100);
        if (outputPath.isEmpty()) {
            appendLog(tr("下载完成。"));
            setStatus(tr("下载完成。"));
        } else {
            appendLog(tr("下载完成：%1").arg(QDir::toNativeSeparators(outputPath)));
            // 状态文字里只放文件名：整条路径通常很长，会把这一行撑得很难看。
            setStatus(tr("下载完成：%1").arg(QFileInfo(outputPath).fileName()));
        }
    } else {
        appendLog(tr("下载失败（退出码 %1）。").arg(exitCode));
        setProgress(0);
        setStatus(tr("下载失败（退出码 %1）。").arg(exitCode));
    }

    updateBusyState();
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
    if (m_resolver->isRunning()) {
        appendLog(tr("正在取消…"));
        setStatus(tr("正在取消…"));
        m_resolver->cancel();
        return;
    }

    if (!m_runner->isRunning()) {
        return;
    }
    appendLog(tr("正在取消…（已下载的临时文件可能残留在保存目录）"));
    setStatus(tr("正在取消…"));
    m_runner->cancel();
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
