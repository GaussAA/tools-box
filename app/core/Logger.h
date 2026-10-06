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
// - 每条日志可带一个**追踪 ID**（trace id），把一次操作的多条日志串成一条线，
//   详见 Logger::beginTrace()。

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

class Logger
{
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

    // ── 追踪 ID（trace id）────────────────────────────────────────────────────
    //
    // 干什么用：一次操作（一次下载、一次内核安装、一次抖音渲染）会写出几十行日志，
    // 而不同操作的日志在文件里是**交错**的。没有 id 时只能靠时间戳猜哪几行属于同
    // 一次；有了 id，`grep <trace id>` 就能把一条链路整段捞出来。
    //
    // 存的是**线程局部**值，不是全局的：本程序界面只有一个线程，QProcess 与网络回调
    // 也都回到这个线程，所以按线程存既够用、又不需要加锁（加锁会把每条日志都变成
    // 一次争用）。反过来也意味着：**跨线程不会自动继承 id**，需要的话由调用方把
    // traceId() 传过去再用 setTraceId() 设上。
    //
    // 典型用法 —— 用 LogTrace 而不是手写 begin/end，免得中途 return 忘了清：
    // @code
    // void VideoDlPage::startDownload()
    // {
    //     const toolbox::LogTrace trace(QStringLiteral("download"));
    //     toolbox::Logger::info(QStringLiteral("开始下载"));
    //     ...
    // }
    // @endcode

    /// 当前线程的追踪 ID；空串表示本次调用链没有开启追踪。
    static QString traceId();

    /// 沿用外部给定的 id（例如要把子进程那边的日志与本次操作关联起来）。
    static void setTraceId(const QString &id);

    /// 生成一个新 id 并设为当前线程的活动 id，返回它。
    /// scope 只是让 id 可读（"download-a1b2c3"），不参与唯一性 —— 唯一性由随机后缀保证。
    static QString beginTrace(const QString &scope = QString());

    /// 结束追踪（清空当前线程的 id）。
    static void endTrace();

private:
    Logger() = default;

    static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                               const QString &msg);

    static bool s_installed;
};

/// 追踪 ID 的 RAII 守卫：构造时开一次追踪，析构时结束。
///
/// 存在的理由是「忘了结束」这条退化路径 —— 手写 beginTrace()/endTrace() 时，中间
/// 任何一个提前 return 或抛路径都会把 id 留下，于是**后面所有无关的日志都挂着一个
/// 早就结束的 id**，追踪反而变成误导。用守卫则无论怎么退出都清得干净。
///
/// 不可拷贝（拷贝会导致同一 id 被清两次、第二次清掉的是别人的 id）。
class LogTrace
{
public:
    /// scope 只影响 id 的可读前缀，省略则 id 是一串纯十六进制。
    explicit LogTrace(const QString &scope = QString())
        : m_id(Logger::beginTrace(scope))
    {}

    ~LogTrace() { Logger::endTrace(); }

    LogTrace(const LogTrace &) = delete;
    LogTrace &operator=(const LogTrace &) = delete;

    /// 本次追踪的 id，方便写进用户可见的提示或传给子进程。
    const QString &id() const { return m_id; }

private:
    QString m_id;
};

} // namespace toolbox
