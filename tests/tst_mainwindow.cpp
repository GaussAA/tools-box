// MainWindow 烟雾测试：验证外壳在「无真实插件」环境下的四条主路径不崩溃。
//
// 覆盖范围（见 docs/best-practices-assessment.md §3.4）：
//   1. 构造 MainWindow（会触发 reloadTools 扫描默认 tools 目录，测试环境多半不存在）；
//   2. ToolRegistry::rescan 指向不存在目录时返回 0 并记录错误；
//   3. 切到首页（pageIndex 0）不崩；
//   4. 搜索过滤（输入关键字 + 清空）不崩。
//
// 不部署真实插件 DLL：videodl 的 createPage 可能启动外部子进程/网络，
// 与 tests/CMakeLists.txt「不允许依赖真实网络或真实子进程」的约定冲突，
// 端到端加载留给集成测试。

#include "MainWindow.h"
#include "ToolRegistry.h"
#include "core/Logger.h"

#include <QApplication>
#include <QDir>
#include <QListWidget>
#include <QMetaObject>
#include <QSettings>
#include <QStackedWidget>
#include <QTest>

class TestMainWindow : public QObject
{
    Q_OBJECT

private slots:
    void constructWithoutPluginsDoesNotCrash();
    void rescanMissingDirReportsErrorAndZero();
    void switchToHomePageDoesNotCrash();
    void searchFilterDoesNotCrash();
};

// 构造 MainWindow 会触发 reloadTools() 扫描默认插件目录（测试环境多半不存在），
// 验证外壳能优雅处理「无插件」：构造不崩、导航至少含「首页」、首页页存在。
void TestMainWindow::constructWithoutPluginsDoesNotCrash()
{
    MainWindow w;

    auto *stack = w.findChild<QStackedWidget *>();
    QVERIFY2(stack != nullptr, "QStackedWidget 未创建");
    // 0 号位固定是首页，无插件时整个 stack 只有这一页。
    QCOMPARE(stack->count(), 1);

    auto *nav = w.findChild<QListWidget *>();
    QVERIFY2(nav != nullptr, "导航 QListWidget 未创建");
    // 至少「首页」一项；无插件时无收藏/最近使用/分类分区。
    QVERIFY(nav->count() >= 1);
}

// ToolRegistry 单独验证：指向不存在的目录应返回 0 且记录错误，而不是崩溃。
void TestMainWindow::rescanMissingDirReportsErrorAndZero()
{
    ToolRegistry reg;
    const int n = reg.rescan(QStringLiteral("C:/__toolbox_test_nonexistent_dir__/xyz"));
    QCOMPARE(n, 0);
    QVERIFY(!reg.errors().isEmpty());
}

// 切到首页（pageIndex 0）不应崩溃；首页是 homePage 占位 QLabel。
void TestMainWindow::switchToHomePageDoesNotCrash()
{
    MainWindow w;
    // onNavRowChanged 是私有槽，借元对象系统调用（仍在 moc 注册范围）。
    const bool ok = QMetaObject::invokeMethod(&w, "onNavRowChanged", Q_ARG(int, 0));
    QVERIFY(ok);
}

// 搜索过滤：先输入关键字，再清空，两次都不应崩溃。
void TestMainWindow::searchFilterDoesNotCrash()
{
    MainWindow w;
    QMetaObject::invokeMethod(&w, "onSearchTextChanged", Q_ARG(QString, QStringLiteral("base64")));
    QMetaObject::invokeMethod(&w, "onSearchTextChanged", Q_ARG(QString, QString()));
}

int main(int argc, char *argv[])
{
    // 无头环境跑 GUI 测试必须指定 offscreen 平台，且要在 QApplication 构造前设置。
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // 把 QSettings 引到临时 ini，避免烟雾测试读写污染真实注册表/配置。
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString iniDir = QDir::tempPath() + QStringLiteral("/toolbox_test_settings");
    QDir().mkpath(iniDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, iniDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, iniDir);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ToolBoxTest"));
    QApplication::setApplicationName(QStringLiteral("ToolBoxTest"));

    toolbox::Logger::install(iniDir + QStringLiteral("/toolbox_test.log"));

    return QTest::qExec(new TestMainWindow, argc, argv);
}

#include "tst_mainwindow.moc"
