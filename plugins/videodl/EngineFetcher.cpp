#include "EngineFetcher.h"

#include "core/EngineCheck.h"
#include "core/EngineFetchPolicy.h"
#include "core/EngineInstall.h"
#include "core/EngineLocator.h"
#include "core/OutputParsing.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>

namespace {

/// 内核下载最多发起几次传输（失败会带着断点续传重试）。
constexpr int kMaxFetchAttempts = 5;

/// 单次传输的超时（毫秒）。没有它，连接被中间设备挂住时会永远卡在某个百分比上不报错。
constexpr int kTransferTimeoutMs = 30000;

/// 失败后隔多久重连（毫秒）。立刻重连容易又撞上同一个坏连接。
constexpr int kRetryDelayMs = 1500;

/// 内核的落地目录：<程序目录>/tools/bin。
QString engineDir()
{
    return videodl::engineDir(QCoreApplication::applicationDirPath());
}

/// 把路径包成 PowerShell 的单引号字符串字面量。
///
/// 单引号串里的单引号靠**写两遍**来转义（不是反斜杠）。少了这一步，临时目录路径里
/// 只要有一个 `'`（Windows 用户名允许这个字符），整条 -Command 就会被截断，
/// 症状是「解压失败」而真因根本没显示在日志里。
QString psSingleQuoted(const QString &value)
{
    return QStringLiteral("'%1'").arg(
        QString(value).replace(QLatin1Char('\''), QStringLiteral("''")));
}

} // namespace

EngineFetcher::EngineFetcher(QObject *parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{}

EngineFetcher::~EngineFetcher()
{
    if (m_unzip && m_unzip->state() != QProcess::NotRunning) {
        m_unzip->kill();
        m_unzip->waitForFinished(2000);
    }
    delete m_file;
    m_file = nullptr;
    if (m_reply) {
        m_reply->abort();
    }
}

QString EngineFetcher::kindLabel() const
{
    return m_kind == Kind::Ffmpeg ? tr("ffmpeg") : tr("yt-dlp");
}

void EngineFetcher::start(Kind kind, const QString &url, const QString &targetPath)
{
    if (m_kind != Kind::None) {
        return;
    }

    m_kind = kind;
    m_url = url;
    m_targetPath = targetPath;
    m_partPath = targetPath + QStringLiteral(".part");
    m_attempt = 0;
    m_offset = 0;
    m_cancelled = false;

    QFile::remove(m_partPath);

    emit logLine(tr("开始下载 %1 …").arg(kindLabel()));
    emit progress(0);
    emit status(tr("正在下载 %1 …").arg(kindLabel()));
    beginTransfer();
}

void EngineFetcher::cancel()
{
    if (m_kind == Kind::None) {
        return;
    }

    // 与「传输失败」分开处理：失败会重试并报失败，取消就是取消。
    m_cancelled = true;
    if (m_reply) {
        // abort() 会立刻发出 finished，收尾交给 onReplyFinished 的取消分支。
        m_reply->abort();
        return;
    }
    reset();
}

void EngineFetcher::beginTransfer()
{
    // 重试定时器可能在「用户已经取消」之后才触发（等的那 1.5 秒里点了取消），
    // 这时不该又悄悄把下载拉起来。
    if (m_kind == Kind::None) {
        return;
    }

    ++m_attempt;

    delete m_file;
    m_file = nullptr;

    // 上次已经落盘的部分继续用，从它的末尾往后接着要。
    qint64 offset = 0;
    const QFileInfo partInfo(m_partPath);
    if (partInfo.exists() && partInfo.size() > 0) {
        offset = partInfo.size();
    }
    m_offset = offset;

    m_file = new QFile(m_partPath, this);
    const QIODevice::OpenMode mode =
        offset > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly;
    if (!m_file->open(mode)) {
        emit logLine(tr("无法写入临时文件：%1").arg(QDir::toNativeSeparators(m_partPath)));
        // 先复位再发信号：页面收到 finished 时会立刻读 isBusy() 刷新按钮，
        // 顺序反了就会把按钮留在忙碌态。
        const Kind kind = m_kind;
        m_kind = Kind::None;
        reset();
        emit finished(false, kind, m_targetPath);
        return;
    }

    QNetworkRequest request{QUrl(m_url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // GitHub 的 release 资源会跳到 release-assets.githubusercontent.com，
    // Qt 默认走 HTTP/2 连那个主机时容易一个字节都收不到，退回 HTTP/1.1 更稳。
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    request.setTransferTimeout(kTransferTimeoutMs);
    if (offset > 0) {
        request.setRawHeader("Range",
                             "bytes=" + QByteArray::number(offset) + QByteArrayLiteral("-"));
    }

    QNetworkReply *reply = m_net->get(request);
    m_reply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, &EngineFetcher::onReplyProgress);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_file) {
            m_file->write(reply->readAll());
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onReplyFinished(reply); });
}

void EngineFetcher::scheduleRetry()
{
    m_reply = nullptr;
    delete m_file;
    m_file = nullptr;

    // 稍等一下再重连，避免立刻又撞上同一个坏连接。
    emit status(tr("%1 下载中断，正在重试（第 %2 次）…").arg(kindLabel()).arg(m_attempt + 1));
    QTimer::singleShot(kRetryDelayMs, this, &EngineFetcher::beginTransfer);
}

void EngineFetcher::onReplyProgress(qint64 received, qint64 total)
{
    if (total <= 0) {
        return;
    }
    // 续传时 received/total 只统计本次请求的范围，得把已落盘的部分加回去。
    const qint64 done = m_offset + received;
    const qint64 all = m_offset + total;
    const int percent = static_cast<int>(done * 100 / all);
    emit progress(percent);

    const auto mb = [](qint64 bytes) {
        return QString::number(static_cast<double>(bytes) / (1024.0 * 1024.0), 'f', 1);
    };
    emit status(
        tr("正在下载 %1 … %2%（%3 / %4 MB）").arg(kindLabel()).arg(percent).arg(mb(done), mb(all)));
}

void EngineFetcher::onReplyFinished(QNetworkReply *reply)
{
    delete m_file;
    m_file = nullptr;
    if (reply) {
        reply->deleteLater();
    }
    m_reply = nullptr;

    const int statusCode =
        reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
    const QNetworkReply::NetworkError error =
        reply ? reply->error() : QNetworkReply::UnknownNetworkError;
    const QString errorText = reply ? reply->errorString() : tr("网络请求已取消");

    // 「下一步怎么办」交给 core 里的纯函数判定（tests/tst_enginefetchpolicy 逐条钉住）：
    // 这里只负责把事实凑齐、把结论翻译成日志与信号。
    videodl::FetchFacts facts;
    facts.userCancelled = m_cancelled;
    m_cancelled = false;
    facts.transportFailed = !reply || error != QNetworkReply::NoError;
    facts.statusCode = statusCode;
    facts.offset = m_offset;
    facts.contentRangeStart = (reply && error == QNetworkReply::NoError && statusCode == 206)
        ? videodl::parseContentRangeStart(QString::fromLatin1(reply->rawHeader("Content-Range")))
        : -1;
    facts.attempt = m_attempt;
    facts.maxAttempts = kMaxFetchAttempts;

    switch (videodl::decideFetchAction(facts)) {
    case videodl::FetchAction::Cancelled:
        // 用户主动取消：不重试、不报失败。
        reset();
        return;
    case videodl::FetchAction::RestartWhole:
        // 两条文案对应两种故障：服务端当没听见 Range，或 206 起点对不上。
        emit logLine(statusCode == 200
                         ? tr("服务器未支持断点续传，重新下载整个文件。")
                         : tr("续传的起始位置与已下载的部分对不上，重新下载整个文件。"));
        QFile::remove(m_partPath);
        m_offset = 0;
        scheduleRetry();
        return;
    case videodl::FetchAction::Retry:
        emit logLine(tr("第 %1 次下载中断（%2），重试中…").arg(m_attempt).arg(errorText));
        scheduleRetry();
        return;
    case videodl::FetchAction::Fail:
        emit logLine(tr("下载失败（已尝试 %1 次）：%2").arg(m_attempt).arg(errorText));
        finish(false);
        return;
    case videodl::FetchAction::Complete:
        break;
    }

    finish(true);
}

void EngineFetcher::finish(bool ok)
{
    const Kind kind = m_kind;
    // 标签要在复位之前取：kindLabel() 读的是 m_kind，复位后就只剩默认值了。
    const QString label = kindLabel();

    // 下面每一处都是「先复位、再发信号」：finished 一到，页面就会读 isBusy()
    // 刷新按钮，顺序反了按钮会卡在忙碌态。
    if (!ok) {
        QFile::remove(m_partPath);
        m_kind = Kind::None;
        reset();
        emit status(tr("%1 下载失败。").arg(label));
        emit finished(false, kind, m_targetPath);
        return;
    }

    // 落地处置整段在 core/EngineInstall（可单测）：正常就位、坏文件删除、
    // 替换失败清理，三条路径都用临时目录验证过（tests/tst_engineinstall.cpp）。
    // 这里只把结论翻译成日志与信号。
    const videodl::EngineInstallResult install = kind == Kind::Ffmpeg
        ? videodl::installFfmpegZip(m_partPath, m_targetPath)
        : videodl::installYtDlp(m_partPath, m_targetPath);
    if (install.status != videodl::EngineInstallStatus::Installed) {
        if (install.status == videodl::EngineInstallStatus::RejectedBadFile) {
            emit logLine(install.problem == videodl::EngineFileProblem::BadHeader
                             ? tr("下载的 %1 文件头不对，不像是可用的文件，已丢弃，请重试。")
                                   .arg(kindLabel())
                             : tr("下载的 %1 只有 %2 字节，不像是完整文件，已丢弃，请重试。")
                                   .arg(kindLabel())
                                   .arg(install.size));
            // 与「保存失败」同一条收尾路径：先复位、再发信号。
            m_kind = Kind::None;
            reset();
            emit status(tr("%1 下载失败。").arg(label));
        } else {
            emit logLine(tr("保存失败：%1").arg(QDir::toNativeSeparators(m_targetPath)));
            m_kind = Kind::None;
            reset();
            emit status(tr("%1 保存失败。").arg(label));
        }
        emit finished(false, kind, m_targetPath);
        return;
    }

    if (kind != Kind::Ffmpeg) {
        emit logLine(tr("yt-dlp 已就绪：%1").arg(QDir::toNativeSeparators(m_targetPath)));
        m_kind = Kind::None;
        reset();
        emit progress(0);
        emit status(tr("yt-dlp 已就绪。"));
        emit finished(true, kind, m_targetPath);
        return;
    }

    // ffmpeg 的 zip 已经改名好了（Expand-Archive 只认 .zip 后缀，
    // 拿 .part 去解压会直接报「不是支持的存档文件格式」），交给解压收尾。
    // 解压阶段没有百分比，切到不确定态。
    emit progress(-1);
    emit status(tr("正在解压 ffmpeg …"));
    extractFfmpeg(m_targetPath);
}

void EngineFetcher::extractFfmpeg(const QString &zipPath)
{
    const QString tmpDir = QDir::tempPath() + QStringLiteral("/toolbox-ffmpeg-extract");
    QDir(tmpDir).removeRecursively();
    QDir().mkpath(tmpDir);

    emit logLine(tr("正在解压 ffmpeg …"));

    auto *ps = new QProcess(this);
    m_unzip = ps;

    const auto cleanup = [tmpDir, zipPath] {
        QDir(tmpDir).removeRecursively();
        QFile::remove(zipPath);
    };

    connect(ps, &QProcess::finished, this,
            [this, ps, tmpDir, zipPath, cleanup](int exitCode, QProcess::ExitStatus) {
                const QString err = videodl::decodeOutput(ps->readAllStandardError()).trimmed();
                ps->deleteLater();
                m_unzip = nullptr;

                QString result;
                bool ok = false;
                if (exitCode != 0) {
                    emit logLine(tr("解压失败：%1").arg(err.isEmpty() ? tr("未知错误") : err));
                    result = tr("ffmpeg 安装失败：解压出错。");
                } else {
                    // 压缩包里有多个同名文件（bin/ 与 doc/），优先挑 bin/ 下的那个。
                    QDirIterator it(tmpDir, QStringList{QStringLiteral("ffmpeg.exe")}, QDir::Files,
                                    QDirIterator::Subdirectories);
                    QString found;
                    QString fallback;
                    while (it.hasNext()) {
                        const QString path = it.next();
                        if (QFileInfo(path).dir().dirName().compare(QStringLiteral("bin"),
                                                                    Qt::CaseInsensitive)
                            == 0) {
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
                        emit logLine(tr("解压后没找到 ffmpeg.exe。"));
                        result = tr("ffmpeg 安装失败：压缩包里没有 ffmpeg.exe。");
                    } else {
                        const QString target = engineDir() + QStringLiteral("/ffmpeg.exe");
                        QFile::remove(target);
                        if (QFile::copy(found, target)) {
                            emit logLine(
                                tr("ffmpeg 已就绪：%1").arg(QDir::toNativeSeparators(target)));
                            result = tr("ffmpeg 已就绪。");
                            ok = true;
                        } else {
                            emit logLine(
                                tr("复制 ffmpeg 失败：%1").arg(QDir::toNativeSeparators(target)));
                            result = tr("ffmpeg 安装失败：无法写入目标目录。");
                        }
                    }
                }

                cleanup();
                const Kind kind = m_kind;
                m_kind = Kind::None;
                reset();
                emit progress(0);
                emit status(result);
                emit finished(ok, kind, m_targetPath);
            });
    connect(ps, &QProcess::errorOccurred, this, [this, ps, cleanup](QProcess::ProcessError) {
        emit logLine(tr("无法调用 PowerShell 解压，请手动指定 ffmpeg 路径。"));
        ps->deleteLater();
        m_unzip = nullptr;
        cleanup();
        const Kind kind = m_kind;
        m_kind = Kind::None;
        reset();
        emit progress(0);
        emit status(tr("ffmpeg 安装失败：无法解压。"));
        emit finished(false, kind, m_targetPath);
    });

    // 用系统自带的 Expand-Archive，省得为解压一个 zip 引入第三方库。
    ps->start(QStringLiteral("powershell.exe"),
              {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
               QStringLiteral("-Command"),
               QStringLiteral("Expand-Archive -LiteralPath %1 -DestinationPath %2 -Force")
                   .arg(psSingleQuoted(QDir::toNativeSeparators(zipPath)),
                        psSingleQuoted(QDir::toNativeSeparators(tmpDir)))});
}

void EngineFetcher::reset()
{
    // 复位必须在**同一个地方**完成：少设一次 m_kind，isBusy() 就一直为真，
    // 页面上的按钮会永久停在忙碌态，而日志看上去一切正常。
    m_kind = Kind::None;
    delete m_file;
    m_file = nullptr;
    m_reply = nullptr;
    m_attempt = 0;
    m_offset = 0;
    QFile::remove(m_partPath);
}
