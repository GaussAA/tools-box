#include "VideoDlPlugin.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const QString kYtDlpDownloadUrl =
    QStringLiteral("https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe");
// 用 GitHub 上的构建，而不是 gyan.dev：gyan.dev 在国内多数网络下几乎拉不动
// （实测 10 分钟只下来 8MB 且随后彻底停住），GitHub 这边能跑到 3MB/s。
const QString kFfmpegDownloadUrl = QStringLiteral(
    "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-gpl.zip");

/// 内核下载最多发起几次传输（失败会带着断点续传重试）。
constexpr int kMaxFetchAttempts = 5;

/// 下载内核的落地目录：<exe 所在目录>/tools/bin。
/// 和插件 DLL 的 tools/ 同处一层，整个 bin/<Config>/ 拷给别人时内核也跟着走。
QString engineDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/tools/bin");
}

/// 按「手动指定 → 随程序目录 → PATH」的顺序定位一个可执行文件。
/// 返回空串表示没找到；调用方据此提示用户或降级。
QString resolveExecutable(const QString &manual, const QString &fileName)
{
    if (!manual.isEmpty() && QFileInfo::exists(manual)) {
        return QFileInfo(manual).absoluteFilePath();
    }

    const QString bundled = engineDir() + QLatin1Char('/') + fileName;
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    // PATH 里可能带扩展名也可能不带，两种写法都试一遍。
    const QString stem = QFileInfo(fileName).completeBaseName();
    for (const QString &name : {stem, fileName}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return found;
        }
    }
    return QString();
}

/// 程序输出可能是 UTF-8，也可能是本地代码页（Windows 控制台默认）。
/// 先用 UTF-8 解，出现替换字符就退回本地编码，避免中文标题变乱码。
QString decodeOutput(const QByteArray &bytes)
{
    const QString utf8 = QString::fromUtf8(bytes);
    if (!utf8.contains(QChar::ReplacementCharacter)) {
        return utf8;
    }
    return QString::fromLocal8Bit(bytes);
}

/// yt-dlp 有时会输出终端配色转义序列，日志里显示出来很脏，直接剥掉。
QString stripAnsi(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    return QString(text).remove(re);
}

bool isDouyinUrl(const QString &url)
{
    return url.contains(QStringLiteral("douyin.com"), Qt::CaseInsensitive);
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

/// 画质下拉框 → 抖音 ratio 参数的映射。抖音只认这几档；
/// 「仅音频」也得先取到视频流再由 ffmpeg 抽音轨，所以同样按最高档拿。
QString douyinRatio(int quality)
{
    switch (quality) {
    case 2:
        return QStringLiteral("720p");
    case 3:
        return QStringLiteral("540p");
    default:
        return QStringLiteral("1080p");
    }
}

/// cookies 文件的规范化副本路径。
QString normalizedCookiesPath()
{
    return QDir::tempPath() + QStringLiteral("/toolbox-cookies.txt");
}

/// 把浏览器导出的 cookies.txt 收拾干净，写到临时文件。
///
/// 浏览器扩展导出 Netscape 格式时经常犯两个错，而 yt-dlp（走 Python 的
/// cookiejar）对这两处很严格，一不合规就拒收整个文件，报一句
/// 「invalid Netscape format cookies file」，让人摸不着头脑：
///   1. 域名以「.」开头（表示对子域也生效），includeSubDomains 列却写成
///      FALSE —— cookiejar 里有断言要求两者一致，直接抛 AssertionError。
///   2. 少量畸形行，cookie 名字是空的。
/// 这里统一修掉：补齐第 2 列、丢掉坏行、写一份干净的临时文件。
/// 原文件本身没问题时也照走一遍，免得行为时好时坏。
/// 返回空串表示读取或写入失败；修正/丢弃的行数通过出参带回，好写进日志。
QString normalizeCookies(const QString &source, int *fixedRows, int *droppedRows)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    in.close();

    const QString target = normalizedCookiesPath();
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }

    QString text = QStringLiteral("# Netscape HTTP Cookie File\n");
    int fixed = 0;
    int dropped = 0;
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        // 原文件的注释一概不要，头部标记由我们统一给；但 #HttpOnly_ 不是
        // 注释，它是 HttpOnly cookie 的标记，必须原样带回去。
        const bool httpOnly = trimmed.startsWith(QStringLiteral("#HttpOnly_"));
        if (trimmed.startsWith(QLatin1Char('#')) && !httpOnly) {
            continue;
        }

        const QString body = httpOnly ? trimmed.mid(10) : trimmed;
        QStringList fields = body.split(QLatin1Char('\t'));
        if (fields.size() < 7 || fields.at(5).trimmed().isEmpty()) {
            ++dropped;
            continue;
        }

        const QString want = fields.at(0).startsWith(QLatin1Char('.'))
                                 ? QStringLiteral("TRUE")
                                 : QStringLiteral("FALSE");
        if (fields.at(1) != want) {
            fields[1] = want;
            ++fixed;
        }

        if (httpOnly) {
            text += QStringLiteral("#HttpOnly_");
        }
        text += fields.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }

    out.write(text.toUtf8());
    out.close();

    if (fixedRows) {
        *fixedRows = fixed;
    }
    if (droppedRows) {
        *droppedRows = dropped;
    }
    return target;
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
    /// 正在进行的「内核下载」任务。用枚举而不是多个 bool，
    /// 是因为同一时刻只允许一个内核下载任务。
    enum class FetchKind { None, YtDlp, Ffmpeg };

    void buildUi();
    void appendLog(const QString &line);
    void setStatus(const QString &text);

    /// 内核下载任务的显示名，用于状态提示与日志。
    QString fetchLabel(FetchKind kind) const;

    void refreshEngineStatus();
    void updateBusyState();
    QString resolvedYtDlp() const;
    QString resolvedFfmpeg() const;

    void pickYtDlp();
    void pickFfmpeg();
    void pickCookies();
    void fetchYtDlp();
    void fetchFfmpeg();
    void startFetch(FetchKind kind, const QString &url, const QString &targetPath);
    void beginFetchTransfer();
    void scheduleFetchRetry();
    void finishFetch(bool ok);
    void onFetchProgress(qint64 received, qint64 total);
    void onFetchFinished(QNetworkReply *reply);
    void extractFfmpeg(const QString &zipPath);

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
    QNetworkAccessManager *m_net = nullptr;

    // 抖音解析用的无头浏览器（只在抖音地址上才会启动）
    QProcess *m_render = nullptr;
    QByteArray m_renderOut;   ///< 浏览器吐出来的整份 DOM
    QString m_renderDir;      ///< 本次下载的保存目录（渲染完接着用）
    int m_renderQuality = 0;
    bool m_renderAudioOnly = false;
    bool m_renderTimedOut = false;
    bool m_renderCancelled = false;
    int m_renderGeneration = 0; ///< 轮次编号，用来让过期的超时定时器失效

    // 下载内核的任务状态
    FetchKind m_fetchKind = FetchKind::None;
    QNetworkReply *m_fetchReply = nullptr;
    QFile *m_fetchFile = nullptr;
    QString m_fetchUrl;
    QString m_fetchPartPath;
    QString m_fetchTargetPath;
    int m_fetchAttempt = 0;   ///< 已发起的传输次数，到 kMaxFetchAttempts 就放弃
    qint64 m_fetchOffset = 0; ///< 本次续传的起始偏移（即已落盘的字节数）

    // 手工指定的路径（留空则回退到随程序目录与 PATH）
    QString m_ytDlpManual;
    QString m_ffmpegManual;

    QByteArray m_pending;   ///< 尚未凑成整行的程序输出
    QString m_lastOutput;   ///< 最近一次识别到的最终文件路径
};

VideoDlPage::VideoDlPage(QWidget *parent)
    : QWidget(parent)
    , m_process(new QProcess(this))
    , m_net(new QNetworkAccessManager(this))
    , m_render(new QProcess(this))
{
    buildUi();

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
    connect(m_render, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_renderTimedOut = true;
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
    QFile::remove(normalizedCookiesPath());
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
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择保存目录"), m_dir->text());
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

QString VideoDlPage::fetchLabel(FetchKind kind) const
{
    return kind == FetchKind::Ffmpeg ? tr("ffmpeg") : tr("yt-dlp");
}

QString VideoDlPage::resolvedYtDlp() const
{
    return resolveExecutable(m_ytDlpManual, QStringLiteral("yt-dlp.exe"));
}

QString VideoDlPage::resolvedFfmpeg() const
{
    return resolveExecutable(m_ffmpegManual, QStringLiteral("ffmpeg.exe"));
}

void VideoDlPage::refreshEngineStatus()
{
    const QString ytDlp = resolvedYtDlp();
    const QString ffmpeg = resolvedFfmpeg();

    const QString ytDlpText = ytDlp.isEmpty()
        ? tr("未找到 —— 点下面的按钮下载，或手动指定路径")
        : QDir::toNativeSeparators(ytDlp);
    const QString ffmpegText = ffmpeg.isEmpty()
        ? tr("未找到 —— 高画质合并与「仅音频」将不可用")
        : QDir::toNativeSeparators(ffmpeg);

    m_engineStatus->setText(tr("yt-dlp：%1\nffmpeg：%2").arg(ytDlpText, ffmpegText));

    updateBusyState();
}

void VideoDlPage::updateBusyState()
{
    const bool videoRunning = m_process->state() != QProcess::NotRunning;
    const bool rendering = m_render->state() != QProcess::NotRunning;
    const bool fetching = m_fetchKind != FetchKind::None;
    const bool busy = videoRunning || rendering || fetching;

    m_download->setEnabled(!busy && !resolvedYtDlp().isEmpty());
    m_cancel->setEnabled(videoRunning || rendering);

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
    startFetch(FetchKind::YtDlp, kYtDlpDownloadUrl,
               engineDir() + QStringLiteral("/yt-dlp.exe"));
}

void VideoDlPage::fetchFfmpeg()
{
    // ffmpeg 官方只发 zip，先落到临时目录，解压后再把 ffmpeg.exe 挑出来。
    const QString zipPath = QDir::tempPath() + QStringLiteral("/toolbox-ffmpeg.zip");
    startFetch(FetchKind::Ffmpeg, kFfmpegDownloadUrl, zipPath);
}

void VideoDlPage::startFetch(FetchKind kind, const QString &url, const QString &targetPath)
{
    if (m_fetchKind != FetchKind::None) {
        return;
    }

    m_fetchKind = kind;
    m_fetchUrl = url;
    m_fetchTargetPath = targetPath;
    m_fetchPartPath = targetPath + QStringLiteral(".part");
    m_fetchAttempt = 0;
    m_fetchOffset = 0;

    QFile::remove(m_fetchPartPath);

    appendLog(tr("开始下载 %1 …").arg(fetchLabel(kind)));
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    setStatus(tr("正在下载 %1 …").arg(fetchLabel(kind)));
    updateBusyState();

    beginFetchTransfer();
}

/// 发起（或重新发起）一次下载传输。
///
/// 这里刻意支持断点续传：ffmpeg 那个包有 190MB 左右，而连 GitHub 的线路
/// 时不时会中途断掉。没有续传的话每次失败都得从 0 重来，基本下不完。
void VideoDlPage::beginFetchTransfer()
{
    ++m_fetchAttempt;

    if (m_fetchFile) {
        m_fetchFile->close();
        delete m_fetchFile;
        m_fetchFile = nullptr;
    }

    // 上次已经落盘的部分继续用，从它的末尾往后接着要。
    qint64 offset = 0;
    const QFileInfo partInfo(m_fetchPartPath);
    if (partInfo.exists() && partInfo.size() > 0) {
        offset = partInfo.size();
    }
    m_fetchOffset = offset;

    m_fetchFile = new QFile(m_fetchPartPath, this);
    const QIODevice::OpenMode mode =
        offset > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly;
    if (!m_fetchFile->open(mode)) {
        appendLog(tr("无法写入临时文件：%1").arg(QDir::toNativeSeparators(m_fetchPartPath)));
        delete m_fetchFile;
        m_fetchFile = nullptr;
        m_fetchKind = FetchKind::None;
        refreshEngineStatus();
        return;
    }

    QNetworkRequest request{QUrl(m_fetchUrl)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // GitHub 的 release 资源会跳到 release-assets.githubusercontent.com，
    // Qt 默认走 HTTP/2 连那个主机时容易一个字节都收不到，退回 HTTP/1.1 更稳。
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    // 没有超时的话，连接被中间设备挂住时会永远卡在某个百分比上不报错。
    // 30 秒收不到任何数据就判定失败，交给重试逻辑处理。
    request.setTransferTimeout(30000);
    if (offset > 0) {
        request.setRawHeader("Range",
                             "bytes=" + QByteArray::number(offset) + QByteArrayLiteral("-"));
    }

    QNetworkReply *reply = m_net->get(request);
    m_fetchReply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, &VideoDlPage::onFetchProgress);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_fetchFile) {
            m_fetchFile->write(reply->readAll());
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        onFetchFinished(reply);
    });
}

void VideoDlPage::scheduleFetchRetry()
{
    m_fetchReply = nullptr;
    if (m_fetchFile) {
        m_fetchFile->close();
        delete m_fetchFile;
        m_fetchFile = nullptr;
    }
    // 稍等一下再重连，避免立刻又撞上同一个坏连接。
    setStatus(tr("%1 下载中断，正在重试（第 %2 次）…")
                  .arg(fetchLabel(m_fetchKind))
                  .arg(m_fetchAttempt + 1));
    QTimer::singleShot(1500, this, [this] { beginFetchTransfer(); });
}

void VideoDlPage::onFetchProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        return;
    }
    // 续传时 received/total 只统计本次请求的范围，得把已落盘的部分加回去。
    const qint64 done = m_fetchOffset + received;
    const qint64 all = m_fetchOffset + total;
    const int percent = static_cast<int>(done * 100 / all);
    m_progress->setValue(percent);

    const auto mb = [](qint64 bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1);
    };
    setStatus(tr("正在下载 %1 … %2%（%3 / %4 MB）")
                  .arg(fetchLabel(m_fetchKind))
                  .arg(percent)
                  .arg(mb(done), mb(all)));
}

void VideoDlPage::onFetchFinished(QNetworkReply *reply)
{
    if (m_fetchFile) {
        m_fetchFile->close();
        delete m_fetchFile;
        m_fetchFile = nullptr;
    }
    if (reply) {
        reply->deleteLater();
    }
    m_fetchReply = nullptr;

    const int statusCode =
        reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    const QNetworkReply::NetworkError error =
        reply ? reply->error() : QNetworkReply::UnknownNetworkError;
    const QString errorText = reply ? reply->errorString() : tr("网络请求已取消");

    // 带了 Range 却收到 200：服务器不支持续传，之前那半截文件对不上了，只能重来。
    if (error == QNetworkReply::NoError && m_fetchOffset > 0 && statusCode == 200) {
        appendLog(tr("服务器未支持断点续传，重新下载整个文件。"));
        QFile::remove(m_fetchPartPath);
        m_fetchOffset = 0;
        scheduleFetchRetry();
        return;
    }

    if (!reply || error != QNetworkReply::NoError) {
        if (m_fetchAttempt < kMaxFetchAttempts) {
            appendLog(tr("第 %1 次下载中断（%2），重试中…").arg(m_fetchAttempt).arg(errorText));
            scheduleFetchRetry();
            return;
        }
        appendLog(tr("下载失败（已尝试 %1 次）：%2").arg(m_fetchAttempt).arg(errorText));
        finishFetch(false);
        return;
    }

    finishFetch(true);
}

/// 收尾：把 .part 落到最终文件名；ffmpeg 还要再把 zip 解出来。
void VideoDlPage::finishFetch(bool ok)
{
    const FetchKind kind = m_fetchKind;
    const QString partPath = m_fetchPartPath;
    const QString targetPath = m_fetchTargetPath;

    const auto stopBusy = [this] {
        m_fetchKind = FetchKind::None;
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        refreshEngineStatus();
    };

    if (!ok) {
        QFile::remove(partPath);
        setStatus(tr("%1 下载失败。").arg(fetchLabel(kind)));
        stopBusy();
        return;
    }

    QFile::remove(targetPath);
    if (!QFile::rename(partPath, targetPath)) {
        appendLog(tr("保存失败：%1").arg(QDir::toNativeSeparators(targetPath)));
        QFile::remove(partPath);
        setStatus(tr("%1 保存失败。").arg(fetchLabel(kind)));
        stopBusy();
        return;
    }

    if (kind == FetchKind::YtDlp) {
        appendLog(tr("yt-dlp 已就绪：%1").arg(QDir::toNativeSeparators(targetPath)));
        stopBusy();
        setStatus(tr("yt-dlp 已就绪。"));
        return;
    }

    // ffmpeg 的 zip 已经改名好了（Expand-Archive 只认 .zip 后缀，
    // 拿 .part 去解压会直接报「不是支持的存档文件格式」），交给解压收尾。
    m_progress->setRange(0, 0); // 解压阶段没有百分比，切到不确定态
    setStatus(tr("正在解压 ffmpeg …"));
    extractFfmpeg(targetPath);
}

void VideoDlPage::extractFfmpeg(const QString &zipPath)
{
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/toolbox-ffmpeg-extract");
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    appendLog(tr("正在解压 ffmpeg …"));

    auto *ps = new QProcess(this);
    connect(ps, &QProcess::finished, this,
            [this, ps, tmpDir, zipPath](int exitCode, QProcess::ExitStatus) {
                const QString err = decodeOutput(ps->readAllStandardError()).trimmed();
                ps->deleteLater();

                QString result;
                if (exitCode != 0) {
                    appendLog(tr("解压失败：%1").arg(err.isEmpty() ? tr("未知错误") : err));
                    result = tr("ffmpeg 安装失败：解压出错。");
                } else {
                    // 压缩包里有多个同名文件（bin/ 与 doc/），优先挑 bin/ 下的那个。
                    QDirIterator it(tmpDir, QStringList{QStringLiteral("ffmpeg.exe")},
                                    QDir::Files, QDirIterator::Subdirectories);
                    QString found;
                    QString fallback;
                    while (it.hasNext()) {
                        const QString path = it.next();
                        if (QFileInfo(path).dir().dirName().compare(QStringLiteral("bin"),
                                                                    Qt::CaseInsensitive) == 0) {
                            found = path;
                            break;
                        }
                        if (fallback.isEmpty()) {
                            fallback = path;
                        }
                    }
                    if (found.isEmpty()) {
                        found = fallback;
                    }

                    if (found.isEmpty()) {
                        appendLog(tr("解压后没找到 ffmpeg.exe。"));
                        result = tr("ffmpeg 安装失败：压缩包里没有 ffmpeg.exe。");
                    } else {
                        const QString target = engineDir() + QStringLiteral("/ffmpeg.exe");
                        QFile::remove(target);
                        if (QFile::copy(found, target)) {
                            appendLog(tr("ffmpeg 已就绪：%1")
                                          .arg(QDir::toNativeSeparators(target)));
                            result = tr("ffmpeg 已就绪。");
                        } else {
                            appendLog(tr("复制 ffmpeg 失败：%1")
                                          .arg(QDir::toNativeSeparators(target)));
                            result = tr("ffmpeg 安装失败：无法写入目标目录。");
                        }
                    }
                }

                QDir(tmpDir).removeRecursively();
                QFile::remove(zipPath);

                m_fetchKind = FetchKind::None;
                m_progress->setRange(0, 100);
                m_progress->setValue(0);
                setStatus(result);
                refreshEngineStatus();
            });
    connect(ps, &QProcess::errorOccurred, this, [this, ps, tmpDir, zipPath](QProcess::ProcessError) {
        appendLog(tr("无法调用 PowerShell 解压，请手动指定 ffmpeg 路径。"));
        ps->deleteLater();
        QDir(tmpDir).removeRecursively();
        QFile::remove(zipPath);
        m_fetchKind = FetchKind::None;
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        setStatus(tr("ffmpeg 安装失败：无法解压。"));
        refreshEngineStatus();
    });

    // 用系统自带的 Expand-Archive，省得为解压一个 zip 引入第三方库。
    ps->start(QStringLiteral("powershell.exe"),
              {QStringLiteral("-NoProfile"),
               QStringLiteral("-NonInteractive"),
               QStringLiteral("-Command"),
               QStringLiteral("Expand-Archive -LiteralPath '%1' -DestinationPath '%2' -Force")
                   .arg(QDir::toNativeSeparators(zipPath), QDir::toNativeSeparators(tmpDir))});
}

void VideoDlPage::pickYtDlp()
{
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择 yt-dlp 可执行文件"), QString(),
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
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择 ffmpeg 可执行文件"), QString(),
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
    const QString file = QFileDialog::getOpenFileName(
        this, tr("选择 cookies.txt"), QString(),
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
    // 口令和说明文字全都混在一起，真正的地址只是其中一小段。
    // 这里先把 http(s) 地址抠出来，用户直接整段粘贴也能用。
    // 字符集按 RFC 3986 的合法字符来，中文和反引号之类不在此列，
    // 于是「…/mevuYgCRF1g/ 复制此链接」这种情况会在中文处自然截断。
    static const QRegularExpression urlRe(QStringLiteral(
        R"(https?://[A-Za-z0-9\-._~:/?#\[\]@!$&'()*+,;=%]+)"));
    const QRegularExpressionMatch urlMatch = urlRe.match(rawInput);
    const QString url = urlMatch.hasMatch() ? urlMatch.captured(0) : rawInput;

    const QString ytDlp = resolvedYtDlp();
    if (ytDlp.isEmpty()) {
        QMessageBox::warning(this, tr("缺少下载内核"),
                             tr("还没有可用的 yt-dlp。请点「下载 / 更新 yt-dlp」，"
                                "或手动指定一个 yt-dlp.exe 路径。"));
        return;
    }

    const QString dir = m_dir->text().trimmed();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        QMessageBox::warning(this, tr("保存目录无效"),
                             tr("请选择一个可写入的保存目录。"));
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
                                 const QString &dir, int quality, bool audioOnly,
                                 bool needsReferer)
{
    const QString ytDlp = resolvedYtDlp();
    const QString ffmpeg = resolvedFfmpeg();

    QStringList args;
    args << QStringLiteral("--newline")     // 让进度按行刷新，才好解析
         << QStringLiteral("--no-playlist") // 只下载当前这一个视频
         << QStringLiteral("-P") << dir;

    if (titleHint.isEmpty()) {
        args << QStringLiteral("-o") << QStringLiteral("%(title)s [%(id)s].%(ext)s");
    } else {
        // 抖音那条路拿到的是直链，generic extractor 只会把文件叫成 video.mp4，
        // 只好把页面标题直接写进输出模板。百分号在模板里有含义，先转义掉。
        const QString safeTitle = QString(titleHint).replace(QLatin1Char('%'),
                                                             QStringLiteral("%%"));
        args << QStringLiteral("-o") << (safeTitle + QStringLiteral(".%(ext)s"));
    }

    if (needsReferer) {
        // 抖音的 CDN 会检查来源，少这个头就会 403。
        args << QStringLiteral("--referer") << QStringLiteral("https://www.douyin.com/");
    }

    // 部分 B站 会员内容、YouTube 年龄限制内容需要浏览器 cookies。
    const QString cookies = m_cookies->text().trimmed();
    if (!cookies.isEmpty() && QFileInfo::exists(cookies)) {
        // 导出扩展产出的文件常常不合规，直接喂给 yt-dlp 会被整份拒收，
        // 先收拾一份干净的副本出来（详见 normalizeCookies 的注释）。
        int fixedRows = 0;
        int droppedRows = 0;
        const QString normalized = normalizeCookies(cookies, &fixedRows, &droppedRows);
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
            args << QStringLiteral("-f")
                 << QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]");
            break;
        case 2:
            args << QStringLiteral("-f")
                 << QStringLiteral("bv*[height<=720]+ba/b[height<=720]");
            break;
        case 3:
            args << QStringLiteral("-f")
                 << QStringLiteral("bv*[height<=480]+ba/b[height<=480]");
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
    appendLog(tr("执行：%1 %2")
                  .arg(QDir::toNativeSeparators(ytDlp), args.join(QLatin1Char(' '))));

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
    if (!isDouyinUrl(url)) {
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
    if (!m_render->waitForStarted(5000)) {
        appendLog(tr("浏览器启动失败：%1").arg(m_render->errorString()));
        setStatus(tr("无法启动浏览器。"));
        m_progress->setRange(0, 100);
        m_progress->setValue(0);
        return true;
    }

    // 正常十几秒就跑完，60 秒还没动静就是卡住了。带上次序号，
    // 免得上一次留下的定时器把这一轮刚启动的进程杀掉。
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
    m_renderCancelled = false;
    m_renderTimedOut = false;

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
    if (timedOut) {
        giveUp(tr("渲染超时：抖音页面没能在 60 秒内就绪。"),
               tr("抖音页面渲染超时。"));
        return;
    }
    if (status != QProcess::NormalExit || exitCode != 0) {
        giveUp(tr("浏览器渲染失败（退出码 %1）。").arg(exitCode),
               tr("抖音页面渲染失败。"));
        return;
    }

    const QString dom = QString::fromUtf8(m_renderOut);
    static const QRegularExpression idRe(QStringLiteral(R"(video_id=([A-Za-z0-9]{16,}))"));
    static const QRegularExpression titleRe(QStringLiteral(R"(<title>([^<]*)</title>)"));
    const QString videoId = idRe.match(dom).captured(1);

    QString title = titleRe.match(dom).captured(1).trimmed();
    // 页面标题形如「#纯欲松弛感 #今日份心动穿搭 - 抖音」，把后缀去掉。
    const QString suffix = QStringLiteral(" - 抖音");
    if (title.endsWith(suffix)) {
        title.chop(suffix.size());
    }

    if (videoId.isEmpty()) {
        giveUp(tr("页面渲染完了，但里面没有 video_id —— 多半是抖音又改版了。"),
               tr("没能从抖音页面里取到播放地址。"));
        return;
    }

    appendLog(tr("已取到 video_id：%1").arg(videoId));
    if (!title.isEmpty()) {
        appendLog(tr("标题：%1").arg(title));
    }

    const QString playUrl = QStringLiteral(
        "https://www.douyin.com/aweme/v1/play/?video_id=%1&ratio=%2&line=0")
        .arg(videoId, douyinRatio(m_renderQuality));
    appendLog(tr("播放地址接口：%1").arg(playUrl));

    launchDownload(playUrl, title, m_renderDir, m_renderQuality, m_renderAudioOnly, true);
}

void VideoDlPage::cancelDownload()
{
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
        handleOutputLine(stripAnsi(decodeOutput(raw)));
    }
}

void VideoDlPage::handleOutputLine(const QString &line)
{
    if (line.trimmed().isEmpty()) {
        return;
    }
    appendLog(line);

    static const QRegularExpression progressRe(
        QStringLiteral(R"(\[download\]\s+(\d+(?:\.\d+)?)%)"));
    const QRegularExpressionMatch progressMatch = progressRe.match(line);
    if (progressMatch.hasMatch()) {
        const double percent = progressMatch.captured(1).toDouble();
        m_progress->setRange(0, 100);
        m_progress->setValue(static_cast<int>(percent));

        // 同一行里还带着速度和预计剩余时间，把它们摘出来一起显示，
        // 免得用户只能盯着一个百分比猜还要等多久。
        static const QRegularExpression speedRe(QStringLiteral(R"(\bat\s+(\S+/s))"));
        static const QRegularExpression etaRe(QStringLiteral(R"(\bETA\s+(\S+))"));
        const QRegularExpressionMatch speedMatch = speedRe.match(line);
        const QRegularExpressionMatch etaMatch = etaRe.match(line);

        const QString speed = speedMatch.hasMatch() ? speedMatch.captured(1) : QString();
        const QString eta = etaMatch.hasMatch() ? etaMatch.captured(1) : QString();
        const QString hint = eta.isEmpty() ? speed : tr("%1，剩余 %2").arg(speed, eta);

        setStatus(hint.isEmpty()
                      ? tr("正在下载… %1%").arg(percent, 0, 'f', 0)
                      : tr("正在下载… %1%（%2）").arg(percent, 0, 'f', 0).arg(hint));
    } else if (line.contains(QStringLiteral("[download] Destination:"))) {
        setStatus(tr("正在下载…"));
    } else if (line.contains(QStringLiteral("[Merger]"))) {
        // 合并音视频分轨要花十几秒到几分钟，这期间没有任何百分比可报，
        // 不切不确定态的话进度条会一直停在 100%，看着像卡死。
        m_progress->setRange(0, 0);
        setStatus(tr("正在合并音视频…"));
    } else if (line.contains(QStringLiteral("[ExtractAudio]"))
               || line.contains(QStringLiteral("[ffmpeg]"))) {
        m_progress->setRange(0, 0);
        setStatus(tr("正在提取音频…"));
    }

    // 记下最终产物：普通下载给 Destination，合并/转码后给的是另一条。
    static const QRegularExpression destRe(QStringLiteral(
        R"RE(^(?:\[download\]|\[ExtractAudio\])\s+Destination:\s+(.+)$|^\[Merger\]\s+Merging formats into\s+"(.+)"$)RE"));
    const QRegularExpressionMatch destMatch = destRe.match(line);
    if (destMatch.hasMatch()) {
        m_lastOutput = destMatch.captured(1).isEmpty() ? destMatch.captured(2)
                                                       : destMatch.captured(1);
    }
}

void VideoDlPage::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    // 规范化出来的那份 cookies 副本里有登录凭据，任务一结束就删掉，
    // 别让它在临时目录里躺着。
    QFile::remove(normalizedCookiesPath());

    // 收尾：把缓冲区里最后没带换行的一行也处理掉。
    if (!m_pending.isEmpty()) {
        handleOutputLine(stripAnsi(decodeOutput(m_pending)));
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
    info.name = QStringLiteral("视频下载");
    info.category = QStringLiteral("媒体工具");
    info.version = QStringLiteral("0.1.0");
    info.description = QStringLiteral(
        "粘贴 B站 / YouTube / 抖音 等视频地址，按画质下载到本地（内核为 yt-dlp）。");
    info.icon = QIcon(QStringLiteral(":/icons/videodl.svg"));
    return info;
}

QWidget *VideoDlPlugin::createPage(QWidget *parent)
{
    return new VideoDlPage(parent);
}

#include "VideoDlPlugin.moc"