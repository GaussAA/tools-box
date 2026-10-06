#include "IChildProcess.h"

#include <QProcess>

namespace {

/// 析构收尾时最多等多久（毫秒）。
///
/// 这是 docs/architecture.md §5 允许的那一类阻塞：显式超时、位于收尾路径、
/// 且不超过 5000 ms。析构时已经没有界面可卡，不等待反而会在进程仍在运行时销毁
/// QProcess，报 "Destroyed while process is still running"。
constexpr int kTerminateWaitMs = 2000;

} // namespace

RealChildProcess::RealChildProcess(QObject *parent)
    : IChildProcess(parent)
    , m_process(new QProcess(this))
{
    // 信号原样转发：基类的信号在派生类里可见，所以可以直接连到它上面，
    // 不需要再手写三个转发槽。
    connect(m_process, &QProcess::readyReadStandardOutput, this,
            &IChildProcess::readyReadStandardOutput);
    connect(m_process, &QProcess::finished, this, &IChildProcess::finished);
    connect(m_process, &QProcess::errorOccurred, this, &IChildProcess::errorOccurred);
}

RealChildProcess::~RealChildProcess()
{
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(kTerminateWaitMs);
    }
}

void RealChildProcess::setChannelMode(ChannelMode mode)
{
    m_process->setProcessChannelMode(mode == ChannelMode::Merged ? QProcess::MergedChannels
                                                                 : QProcess::SeparateChannels);
}

void RealChildProcess::setEnvironment(const QProcessEnvironment &env)
{
    m_process->setProcessEnvironment(env);
}

void RealChildProcess::start(const QString &program, const QStringList &args)
{
    m_process->start(program, args);
}

void RealChildProcess::kill()
{
    m_process->kill();
}

bool RealChildProcess::isRunning() const
{
    return m_process->state() != QProcess::NotRunning;
}

QByteArray RealChildProcess::readAllStandardOutput()
{
    return m_process->readAllStandardOutput();
}

QByteArray RealChildProcess::readAllStandardError()
{
    return m_process->readAllStandardError();
}

QString RealChildProcess::errorString() const
{
    return m_process->errorString();
}
