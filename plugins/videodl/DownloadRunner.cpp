#include "DownloadRunner.h"

#include "core/OutputParsing.h"

#include <QProcessEnvironment>

namespace {

/// yt-dlp 是 Python 打包的：不强制 UTF-8，中文标题在 Windows 控制台下会变问号。
QProcessEnvironment pythonUtf8Environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("PYTHONUTF8"), QStringLiteral("1"));
    env.insert(QStringLiteral("PYTHONIOENCODING"), QStringLiteral("utf-8"));
    return env;
}

} // namespace

DownloadRunner::DownloadRunner(QObject *parent)
    : QObject(parent)
    , m_process(new RealChildProcess(this))
    , m_ownsProcess(true)
{
    // 合并通道：yt-dlp 的进度与报错混在 stdout/stderr 两边，分开收会漏行。
    m_process->setChannelMode(IChildProcess::ChannelMode::Merged);
    connect(m_process, &IChildProcess::readyReadStandardOutput, this, &DownloadRunner::onReadyRead);
    connect(m_process, &IChildProcess::finished, this, &DownloadRunner::onFinished);
}

DownloadRunner::DownloadRunner(IChildProcess *process, QObject *parent)
    : QObject(parent)
    , m_process(process)
    , m_ownsProcess(false)
{
    m_process->setChannelMode(IChildProcess::ChannelMode::Merged);
    connect(m_process, &IChildProcess::readyReadStandardOutput, this, &DownloadRunner::onReadyRead);
    connect(m_process, &IChildProcess::finished, this, &DownloadRunner::onFinished);
}

DownloadRunner::~DownloadRunner()
{
    if (m_ownsProcess) {
        delete m_process;
        m_process = nullptr;
    }
}

bool DownloadRunner::isRunning() const
{
    return m_process->isRunning();
}

void DownloadRunner::start(const QString &ytDlp, const QStringList &args)
{
    m_splitter = videodl::LineSplitter();
    m_outputPath.clear();

    m_process->setEnvironment(pythonUtf8Environment());
    m_process->start(ytDlp, args);
}

void DownloadRunner::cancel()
{
    if (m_process->isRunning()) {
        m_process->kill();
    }
}

void DownloadRunner::onReadyRead()
{
    // 凑行交给 core/LineSplitter：半行、\r\n 跨块、\r 尾巴都在那里处理且有单测，
    // 这里只负责把得到的每一行解码后送进 handleLine。
    const QList<QByteArray> lines = m_splitter.append(m_process->readAllStandardOutput());
    for (const QByteArray &raw : lines) {
        handleLine(videodl::stripAnsi(videodl::decodeOutput(raw)));
    }
}

void DownloadRunner::handleLine(const QString &line)
{
    if (line.trimmed().isEmpty()) {
        return;
    }
    emit logLine(line);

    // 解析规则都在 videodl_core，这里只负责把结果翻译成进度与阶段文案 ——
    // 文案要 tr()，属于 View 这一侧。
    // 局部变量不能叫 progress：那会遮蔽同名信号，emit 出来的就不是信号了。
    const videodl::ProgressInfo info = videodl::parseProgress(line);
    if (info.matched) {
        emit progress(info.percent);

        const QString hint =
            info.eta.isEmpty() ? info.speed : tr("%1，剩余 %2").arg(info.speed, info.eta);
        emit status(hint.isEmpty() ? tr("正在下载… %1%").arg(info.percent)
                                   : tr("正在下载… %1%（%2）").arg(info.percent).arg(hint));
    } else {
        switch (videodl::classifyStage(line)) {
        case videodl::OutputStage::DownloadStarting:
            emit status(tr("正在下载…"));
            break;
        case videodl::OutputStage::Merging:
            // 合并要花十几秒到几分钟，这期间没有任何百分比可报，
            // 不切不确定态的话进度条会一直停在 100%，看着像卡死。
            emit progress(-1);
            emit status(tr("正在合并音视频…"));
            break;
        case videodl::OutputStage::ExtractingAudio:
            emit progress(-1);
            emit status(tr("正在提取音频…"));
            break;
        case videodl::OutputStage::None:
            break;
        }
    }

    // 记下最终产物：普通下载给 Destination，合并/转码后给的是另一条。
    const QString destination = videodl::parseDestination(line);
    if (!destination.isEmpty()) {
        m_outputPath = destination;
    }
}

void DownloadRunner::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // 收尾：最后一行常常没有换行，flush 才能把它取出来。
    const QByteArray tail = m_splitter.flush();
    if (!tail.isEmpty()) {
        handleLine(videodl::stripAnsi(videodl::decodeOutput(tail)));
    }

    emit finished(exitCode, exitStatus == QProcess::CrashExit, m_outputPath);
}
