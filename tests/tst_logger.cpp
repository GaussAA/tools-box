#include "core/Logger.h"

#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

class TestLogger : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void installRedirectsQtLogToFile();
    void installIsIdempotent();
    void levelFilterDropsBelowThreshold();
    void environmentOverridesLevel();
    void rotationCreatesBackups();
    void traceIdTagsEveryLineOfOneOperation();
    void noTraceFieldWhenNotTracing();
    void endTraceStopsTagging();
    void logTraceGuardClearsOnScopeExit();
    void beginTraceYieldsDistinctIds();
    void traceIdDoesNotLeakAcrossThreads();
};

namespace {
QString readAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QString s = QString::fromUtf8(f.readAll());
    f.close();
    return s;
}
} // namespace

namespace {
/// 在**另一个线程**里写一行日志。用来验证追踪 ID 是线程局部的，不会自动继承。
class LogFromThread : public QThread
{
public:
    void run() override { toolbox::Logger::info(QStringLiteral("from-worker-thread")); }
};
} // namespace

// 每个用例前后都复位 Logger 与环境变量，保证用例之间完全隔离
// （install 幂等，必须经 shutdown 才能以新配置重装）。
//
// 追踪 ID 是 **thread_local** 的，shutdown() 清不掉它 —— 那属于「另一条链路还在
// 进行中」的正常情况。所以测试必须自己 endTrace()，否则上一个用例残留的 id 会让
// 下一个用例的断言凭空变绿（「日志里含 trace 字段」这种断言尤其容易假绿）。
void TestLogger::init()
{
    toolbox::Logger::shutdown();
    toolbox::Logger::endTrace();
    qunsetenv("TOOLBOX_LOG_LEVEL");
}

void TestLogger::cleanup()
{
    toolbox::Logger::shutdown();
    toolbox::Logger::endTrace();
    qunsetenv("TOOLBOX_LOG_LEVEL");
}

// 验证 install() 后，经由 Qt 原生宏写出的日志被重定向到磁盘文件，且带有级别标记
// 与内容。这是结构化日志「附加可观测层」的核心契约。
void TestLogger::installRedirectsQtLogToFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/toolbox.log");

    toolbox::Logger::install(logPath);

    qInfo() << "structured-log-probe-line";

    const QString content = readAll(logPath);
    QVERIFY(content.contains(QStringLiteral("INFO")));
    QVERIFY(content.contains(QStringLiteral("structured-log-probe-line")));
}

// 验证 install() 是幂等的：重复调用不应切换到另一个文件。
void TestLogger::installIsIdempotent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString first = dir.path() + QStringLiteral("/first.log");
    const QString second = dir.path() + QStringLiteral("/second.log");

    toolbox::Logger::install(first);
    toolbox::Logger::install(second); // 应被忽略

    qInfo() << "idempotent-probe";

    QVERIFY(readAll(first).contains(QStringLiteral("idempotent-probe")));
    QVERIFY2(!QFile::exists(second), "重复 install 不应切换到第二个文件");
}

// 分级开关：minLevel=Warning 时，Debug/Info 消息被丢弃，Warning/Critical 落盘。
void TestLogger::levelFilterDropsBelowThreshold()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/level.log");

    toolbox::LoggerOptions opts;
    opts.minLevel = toolbox::LogLevel::Warning;
    toolbox::Logger::install(logPath, opts);

    toolbox::Logger::debug(QStringLiteral("dbg-should-be-dropped"));
    toolbox::Logger::info(QStringLiteral("info-should-be-dropped"));
    toolbox::Logger::warning(QStringLiteral("warn-should-appear"));
    toolbox::Logger::error(QStringLiteral("error-should-appear"));

    const QString content = readAll(logPath);
    QVERIFY2(!content.contains(QStringLiteral("dbg-should-be-dropped")), "Debug 应被过滤");
    QVERIFY2(!content.contains(QStringLiteral("info-should-be-dropped")), "Info 应被过滤");
    QVERIFY2(content.contains(QStringLiteral("warn-should-appear")), "Warning 应落盘");
    QVERIFY2(content.contains(QStringLiteral("error-should-appear")), "Critical 应落盘");
}

// 环境变量 TOOLBOX_LOG_LEVEL 优先于 options.minLevel（便于运维临时调整）。
void TestLogger::environmentOverridesLevel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/env.log");

    qputenv("TOOLBOX_LOG_LEVEL", "debug");
    toolbox::LoggerOptions opts;
    opts.minLevel = toolbox::LogLevel::Critical; // 应被环境变量覆盖为 Debug
    toolbox::Logger::install(logPath, opts);

    toolbox::Logger::debug(QStringLiteral("debug-via-env"));

    QVERIFY(readAll(logPath).contains(QStringLiteral("debug-via-env")));
}

// 轮转：小上限 + backupCount=2 时，持续写入应产生 .1/.2 且不产生 .3。
void TestLogger::rotationCreatesBackups()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/rot.log");

    toolbox::LoggerOptions opts;
    opts.maxFileSize = 512;
    opts.backupCount = 2;
    toolbox::Logger::install(logPath, opts);

    // 每条约 100 字节，写 200 条远超上限，必然多次轮转。
    const QString filler = QStringLiteral("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    for (int i = 0; i < 200; ++i) {
        toolbox::Logger::info(QStringLiteral("rot-%1-%2").arg(i).arg(filler));
    }

    QVERIFY2(QFile::exists(logPath), "主日志文件应存在");
    QVERIFY2(QFile::exists(logPath + QStringLiteral(".1")), "应产生 .1 备份");
    QVERIFY2(QFile::exists(logPath + QStringLiteral(".2")), "应产生 .2 备份");
    QVERIFY2(!QFile::exists(logPath + QStringLiteral(".3")), "备份份数不应超过 backupCount=2");
    QVERIFY(!readAll(logPath + QStringLiteral(".1")).isEmpty());
}

// 追踪 ID 的核心契约：一次操作写出的**每一条**日志都带同一个 id，
// 于是 grep 这个 id 就能把整条链路捞出来（否则交叉的几十行只能靠时间戳猜）。
void TestLogger::traceIdTagsEveryLineOfOneOperation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/trace.log");
    toolbox::Logger::install(logPath);

    const QString id = toolbox::Logger::beginTrace(QStringLiteral("download"));
    toolbox::Logger::info(QStringLiteral("stage-one"));
    toolbox::Logger::info(QStringLiteral("stage-two"));

    const QString content = readAll(logPath);
    QVERIFY2(content.contains(QStringLiteral("[trace download-")), "应带可读前缀");
    // 两条都要带，且带的是**同一个** id —— 只算出现次数等于 2 即同时验证了这两点。
    QCOMPARE(content.count(QStringLiteral("[trace ") + id + QStringLiteral("]")), 2);
}

// 没开追踪时不应出现空的 [trace ] 字段：绝大多数日志不属于任何链路，
// 给它们加空括号只会让每行更冗长。
void TestLogger::noTraceFieldWhenNotTracing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/notrace.log");
    toolbox::Logger::install(logPath);

    toolbox::Logger::info(QStringLiteral("plain-line"));

    const QString content = readAll(logPath);
    QVERIFY(content.contains(QStringLiteral("plain-line")));
    QVERIFY2(!content.contains(QStringLiteral("[trace")), "未开追踪时不该有 trace 字段");
}

// endTrace() 之后写的日志不再带 id —— 否则一次操作结束后，后面所有无关日志
// 都会挂着一个早就结束的 id，追踪反而变成误导。
void TestLogger::endTraceStopsTagging()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/endtrace.log");
    toolbox::Logger::install(logPath);

    toolbox::Logger::beginTrace(QStringLiteral("scoped"));
    toolbox::Logger::info(QStringLiteral("inside-trace"));
    toolbox::Logger::endTrace();
    toolbox::Logger::info(QStringLiteral("after-trace"));

    const QString content = readAll(logPath);
    QVERIFY(content.contains(QStringLiteral("inside-trace")));
    QVERIFY(content.contains(QStringLiteral("after-trace")));

    // 按行判定而不是全文 count：id 是随机的，子串匹配容易把「追踪期内那行」和
    // 「之后那行」混在一起判。
    bool insideTagged = false;
    bool afterTagged = false;
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("inside-trace"))) {
            insideTagged = line.contains(QStringLiteral("[trace scoped-"));
        }
        if (line.contains(QStringLiteral("after-trace"))) {
            afterTagged = line.contains(QStringLiteral("[trace"));
        }
    }
    QVERIFY2(insideTagged, "追踪期内的那行应带 id");
    QVERIFY2(!afterTagged, "endTrace 之后不应再带 id");
}

// LogTrace 守卫的契约：出了作用域（含任何提前 return 的路径）必须自动清掉 id。
// 手写 beginTrace/endTrace 最容易漏的就是中途 return，漏了则后续所有日志都被误标。
void TestLogger::logTraceGuardClearsOnScopeExit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/guard.log");
    toolbox::Logger::install(logPath);

    {
        const toolbox::LogTrace trace(QStringLiteral("guarded"));
        toolbox::Logger::info(QStringLiteral("inside-guard"));
        QVERIFY2(trace.id().startsWith(QStringLiteral("guarded-")), "守卫应暴露可读的 id");
    }
    toolbox::Logger::info(QStringLiteral("outside-guard"));

    const QString content = readAll(logPath);
    QVERIFY(content.contains(QStringLiteral("[trace guarded-")));

    // 守卫结束后写的那一行不得再带 trace 字段。按行判定，避免子串误判。
    bool outsideLineHasTrace = false;
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("outside-guard"))) {
            outsideLineHasTrace = line.contains(QStringLiteral("[trace"));
        }
    }
    QVERIFY2(!outsideLineHasTrace, "守卫析构后不应再带 trace 字段");
}

// 连续两次 beginTrace 必须给出不同 id —— 否则「同一次操作」这个前提就不成立了。
void TestLogger::beginTraceYieldsDistinctIds()
{
    const QString a = toolbox::Logger::beginTrace(QStringLiteral("op"));
    const QString b = toolbox::Logger::beginTrace(QStringLiteral("op"));
    QVERIFY2(a != b, "两次追踪不应拿到同一个 id");
    QVERIFY(a.startsWith(QStringLiteral("op-")));
}

// 追踪 ID 是线程局部的：主线程开的追踪不会自动跟到工作线程。
// 这条钉住的是设计取舍本身 —— 若哪天改成进程全局，本用例会先红。
void TestLogger::traceIdDoesNotLeakAcrossThreads()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString logPath = dir.path() + QStringLiteral("/threads.log");
    toolbox::Logger::install(logPath);

    toolbox::Logger::beginTrace(QStringLiteral("main"));
    LogFromThread worker;
    worker.start();
    QVERIFY(worker.wait(5000));

    bool workerLineHasTrace = false;
    const QString content = readAll(logPath);
    for (const QString &line : content.split(QLatin1Char('\n'))) {
        if (line.contains(QStringLiteral("from-worker-thread"))) {
            workerLineHasTrace = line.contains(QStringLiteral("[trace"));
        }
    }
    QVERIFY(content.contains(QStringLiteral("from-worker-thread")));
    QVERIFY2(!workerLineHasTrace, "追踪 ID 不应跨线程继承");
}

QTEST_GUILESS_MAIN(TestLogger)

#include "tst_logger.moc"
