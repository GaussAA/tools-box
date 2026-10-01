#include "core/Logger.h"

#include <QFile>
#include <QTemporaryDir>
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

// 每个用例前后都复位 Logger 与环境变量，保证用例之间完全隔离
// （install 幂等，必须经 shutdown 才能以新配置重装）。
void TestLogger::init()
{
    toolbox::Logger::shutdown();
    qunsetenv("TOOLBOX_LOG_LEVEL");
}

void TestLogger::cleanup()
{
    toolbox::Logger::shutdown();
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

QTEST_GUILESS_MAIN(TestLogger)

#include "tst_logger.moc"
