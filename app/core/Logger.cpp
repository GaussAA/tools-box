#include "Logger.h"

#include <cstdio>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QThread>

namespace toolbox {

bool Logger::s_installed = false;
QString Logger::s_logFilePath;

namespace {

QFile *g_logFile = nullptr;
QMutex g_mutex;
qint64 g_writtenBytes = 0; ///< 当前日志文件已写字节数（含 install 时的初始大小），用于免 stat 判定轮转
qint64 g_maxFileSize = 0;
int g_backupCount = 0;
LogLevel g_minLevel = LogLevel::Info;

/// 把 Qt 消息类型映射为可比较的严重度（越大越严重）。致命级别恒为最高。
int severity(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return 0;
    case QtInfoMsg:     return 1;
    case QtWarningMsg:  return 2;
    case QtCriticalMsg: return 3;
    case QtFatalMsg:    return 4;
    }
    return 1;
}

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:    return QStringLiteral("DEBUG");
    case QtInfoMsg:     return QStringLiteral("INFO");
    case QtWarningMsg:  return QStringLiteral("WARN");
    case QtCriticalMsg: return QStringLiteral("ERROR");
    case QtFatalMsg:    return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

/// 解析环境变量里的级别字符串；无法识别时返回 false（由调用方决定回退值）。
bool parseLevel(const QByteArray &raw, LogLevel &out)
{
    const QByteArray s = raw.trimmed().toLower();
    if (s == "debug") {
        out = LogLevel::Debug;
        return true;
    }
    if (s == "info") {
        out = LogLevel::Info;
        return true;
    }
    if (s == "warn" || s == "warning") {
        out = LogLevel::Warning;
        return true;
    }
    if (s == "error" || s == "critical") {
        out = LogLevel::Critical;
        return true;
    }
    return false;
}

/// 轮转：log → log.1 → … → log.N，超出 N 份的最老备份删除。调用者须已持有 g_mutex。
///
/// 注意：本函数运行在持锁路径内，**绝不能调用任何 Qt 日志宏**（会重入
/// messageHandler 再取同一把非递归锁而自锁），故失败仅以 fprintf 直写 stderr。
void rotateLocked()
{
    if (!g_logFile) {
        return;
    }
    const QString base = g_logFile->fileName();
    if (g_logFile->isOpen()) {
        g_logFile->close();
    }

    for (int i = g_backupCount - 1; i >= 1; --i) {
        const QString from = QStringLiteral("%1.%2").arg(base).arg(i);
        const QString to = QStringLiteral("%1.%2").arg(base).arg(i + 1);
        if (!QFile::exists(from)) {
            continue;
        }
        QFile::remove(to);
        if (!QFile::rename(from, to)) {
            std::fprintf(stderr, "Logger: 备份重命名失败：%s -> %s\n",
                         qPrintable(from), qPrintable(to));
        }
    }

    if (QFile::exists(base)) {
        const QString first = QStringLiteral("%1.1").arg(base);
        QFile::remove(first);
        if (!QFile::rename(base, first)) {
            std::fprintf(stderr, "Logger: 日志轮转失败，将继续写入原文件。\n");
        }
    }

    // 重新打开（可能失败，退化为仅控制台）。
    if (!g_logFile->open(QIODevice::Append | QIODevice::Text | QIODevice::WriteOnly)) {
        delete g_logFile;
        g_logFile = nullptr;
    }
    g_writtenBytes = 0;
}

} // namespace

void Logger::install(const QString &logFilePath, const LoggerOptions &options)
{
    if (s_installed) {
        return;
    }
    s_installed = true;
    s_logFilePath = logFilePath;

    const QFileInfo fi(logFilePath);
    const QDir dir = fi.dir();
    if (!dir.exists()) {
        dir.mkpath(dir.absolutePath());
    }

    g_maxFileSize = options.maxFileSize;
    g_backupCount = options.backupCount;
    g_minLevel = options.minLevel;

    // 环境变量优先，便于运维临时提高/降低日志级别而无需重编译。
    LogLevel fromEnv = LogLevel::Info;
    if (parseLevel(qgetenv("TOOLBOX_LOG_LEVEL"), fromEnv)) {
        g_minLevel = fromEnv;
    }

    g_logFile = new QFile(logFilePath);
    if (g_logFile->open(QIODevice::Append | QIODevice::Text | QIODevice::WriteOnly)) {
        g_writtenBytes = g_logFile->size();
    } else {
        // 日志文件打不开时退化为仅控制台输出，不致命。
        delete g_logFile;
        g_logFile = nullptr;
        g_writtenBytes = 0;
    }

    qInstallMessageHandler(&Logger::messageHandler);

    info(QStringLiteral("Logger 已安装，日志文件：%1（最低级别=%2，单文件上限=%3 字节，保留 %4 份）")
             .arg(logFilePath)
             .arg(static_cast<int>(g_minLevel))
             .arg(g_maxFileSize)
             .arg(g_backupCount));
}

void Logger::shutdown()
{
    {
        QMutexLocker locker(&g_mutex);
        if (g_logFile) {
            if (g_logFile->isOpen()) {
                g_logFile->close();
            }
            delete g_logFile;
            g_logFile = nullptr;
        }
        g_writtenBytes = 0;
    }
    // 先关闭文件、再摘除 handler，避免 handler 访问已释放的 g_logFile。
    qInstallMessageHandler(nullptr);
    s_installed = false;
    s_logFilePath.clear();
}

void Logger::messageHandler(QtMsgType type,
                            const QMessageLogContext &context,
                            const QString &msg)
{
    // 分级过滤：低于阈值的消息直接丢弃（不入文件也不进控制台）；致命消息永不拦。
    if (type != QtFatalMsg && severity(type) < static_cast<int>(g_minLevel)) {
        return;
    }

    const QString timestamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    const QString threadId =
        QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    const QString level = levelName(type);

    QString location;
    if (context.file && context.file[0] != '\0') {
        location = QStringLiteral(" [%1:%2]")
                       .arg(QString::fromUtf8(context.file),
                            QString::number(context.line));
    }

    const QString formatted = QStringLiteral("%1 [%2] [tid %3] %4%5\n")
                                 .arg(timestamp, level, threadId, msg, location);

    // 控制台（stderr）始终输出，方便实时观察。
    {
        QTextStream errStderr(stderr, QIODevice::WriteOnly);
        errStderr << formatted;
        errStderr.flush();
    }

    // 文件写出加锁，避免多线程日志交错；轮转判定同样在锁内完成。
    QMutexLocker locker(&g_mutex);
    if (g_logFile && g_logFile->isOpen()) {
        if (g_maxFileSize > 0 && g_backupCount > 0
            && g_writtenBytes + formatted.size() > g_maxFileSize) {
            rotateLocked();
        }
        if (g_logFile && g_logFile->isOpen()) {
            QTextStream out(g_logFile);
            out << formatted;
            out.flush();
            g_writtenBytes += formatted.size();
        }
    }

    if (type == QtFatalMsg) {
        abort();
    }
}

void Logger::error(const QString &message)   { qCritical().noquote() << message; }
void Logger::warning(const QString &message) { qWarning().noquote() << message; }
void Logger::info(const QString &message)    { qInfo().noquote() << message; }
void Logger::debug(const QString &message)   { qDebug().noquote() << message; }

} // namespace toolbox
