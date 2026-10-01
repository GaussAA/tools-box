#pragma once

#include <QString>
#include <QtGlobal>

namespace toolbox {

// 轻量分级日志：把 Qt 原生的 qDebug/qInfo/qWarning/qCritical 重定向到磁盘文件 +
// 控制台（stderr），附带时间戳、级别、线程 ID 与源码位置（若可用）。
//
// 设计取舍：
// - 不引入信号/槽，纯静态 + 文件写出，避免与 UI 层耦合，也便于在测试中验证。
// - 业务代码无需改动调用点：装好 handler 后，照常使用 Qt 日志宏即可落盘。
// - ToolRegistry 的 m_errors（UI 展示用）保持不变，本日志是「附加」的可观测层，
//   两者互不影响。

/// 日志级别，按严重度递增（Debug < Info < Warning < Critical）。
enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Critical = 3 };

/// 日志行为配置。默认值即产品默认：单文件 5 MiB、保留 3 份备份、最低 Info。
struct LoggerOptions
{
    /// 单文件字节上限，超过即轮转；<=0 表示关闭轮转。
    qint64 maxFileSize = 5 * 1024 * 1024;
    /// 保留的备份份数（log.1 … log.N），超出即删最老一份；<=0 表示关闭轮转。
    int backupCount = 3;
    /// 最低输出级别，低于此级别的消息被丢弃。环境变量 TOOLBOX_LOG_LEVEL 若被
    /// 设置则覆盖本项（便于运维临时调整而不重编译）。
    LogLevel minLevel = LogLevel::Info;
};

class Logger {
public:
    // 安装全局消息处理器并打开日志文件。幂等：重复调用只生效一次；
    // 需要以新配置重装时先调用 shutdown()。
    static void install(const QString &logFilePath, const LoggerOptions &options = LoggerOptions());

    // 卸载消息处理器、关闭日志文件并复位内部状态。用于测试隔离「受控重装」。
    // 生产代码通常无需调用。
    static void shutdown();

    // 便捷级别方法，等价于对应的 Qt 日志宏，但语义更明确，且不依赖调用点的
    // 源码位置信息。
    static void error(const QString &message);
    static void warning(const QString &message);
    static void info(const QString &message);
    static void debug(const QString &message);

private:
    Logger() = default;

    static void messageHandler(QtMsgType type,
                               const QMessageLogContext &context,
                               const QString &msg);

    static bool s_installed;
    static QString s_logFilePath;
};

} // namespace toolbox
