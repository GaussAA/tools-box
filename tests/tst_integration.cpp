// 端到端集成测试：部署真实插件 DLL，验证插件架构端到端正确。
//
// 与 tst_mainwindow（无插件烟雾测试）互补：本测试验证「插件契约真实可用」，即
//   - ToolRegistry 能扫描到真实 DLL 并通过 §3.1 静态元数据门禁；
//   - qobject_cast 跨 DLL 识别 IToolPlugin；
//   - createPage() 在插件 DLL 内 new 出 QWidget 并返回给主程序（跨 DLL 虚调用）；
//   - 主程序 delete 该 widget 时跨 DLL 析构安全（同 Qt 运行时）。
//
// 仅部署 base64_tool / jsonfmt_tool 两个纯 GUI 安全插件（不触发外部进程/网络），
// 符合 tests/CMakeLists.txt「不允许依赖真实网络或真实子进程」约定。

#include "ToolRegistry.h"
#include "ToolBoxPlugin.h"
#include "core/Logger.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QSettings>
#include <QTest>
#include <QWidget>

class TestIntegration : public QObject
{
    Q_OBJECT

private slots:
    void rescanLoadsDeployedPlugins();
    void createPageReturnsValidWidget();
    void metaFieldsMatchDeployed();
    void pageShowAndDeleteCrossDll();
};

namespace {
// 插件部署在测试 exe 旁的 integration_tools/（见 tests/CMakeLists.txt）。
// 刻意不用主程序约定的 tools/ —— 否则 tst_mainwindow 会经 defaultPluginDir()
// 扫到这里的插件，破坏其「无插件」前提（测试间隐式耦合）。
QString deployedPluginDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/integration_tools");
}
} // namespace

// 扫描真实部署目录：base64 + jsonfmt 两个插件应被成功装载，且全部兼容（无加载错误）。
void TestIntegration::rescanLoadsDeployedPlugins()
{
    ToolRegistry reg;
    const int n = reg.rescan(deployedPluginDir());
    QVERIFY2(n >= 2, qPrintable(QStringLiteral("期望至少 2 个插件，实际 %1").arg(n)));
    QVERIFY2(reg.errors().isEmpty(),
             qPrintable(QStringLiteral("插件加载出现错误：%1").arg(reg.errors().join(';'))));
}

// 每个插件的 createPage() 都应返回一个非空的 QWidget（跨 DLL 虚调用 + 跨模块对象创建）。
void TestIntegration::createPageReturnsValidWidget()
{
    ToolRegistry reg;
    QVERIFY(reg.rescan(deployedPluginDir()) >= 2);

    for (const auto &entry : reg.entries()) {
        QWidget *page = entry.plugin->createPage(nullptr);
        QVERIFY2(page != nullptr,
                 qPrintable(QStringLiteral("createPage 返回空：%1").arg(entry.meta.id)));
        QVERIFY(page->inherits("QWidget"));
        delete page; // 跨 DLL 析构：对象在插件 DLL 内 new，由主程序 delete，同 Qt 运行时安全。
    }
}

// 验证静态 meta 字段与部署的插件真值一致（id 必含 text.base64 / dev.json-format，分类非空）。
void TestIntegration::metaFieldsMatchDeployed()
{
    ToolRegistry reg;
    QVERIFY(reg.rescan(deployedPluginDir()) >= 2);

    QStringList ids;
    for (const auto &entry : reg.entries()) {
        ids.append(entry.meta.id);
        QVERIFY2(!entry.meta.category.isEmpty(),
                 qPrintable(QStringLiteral("分类不应为空：%1").arg(entry.meta.id)));
        QVERIFY2(!entry.meta.version.isEmpty(),
                 qPrintable(QStringLiteral("版本不应为空：%1").arg(entry.meta.id)));
    }
    QVERIFY(ids.contains(QStringLiteral("text.base64")));
    QVERIFY(ids.contains(QStringLiteral("dev.json-format")));
}

// createPage 后 show + 跑事件循环 + 跨 DLL delete，验证整条 UI 路径不崩。
void TestIntegration::pageShowAndDeleteCrossDll()
{
    ToolRegistry reg;
    QVERIFY(reg.rescan(deployedPluginDir()) >= 2);

    for (const auto &entry : reg.entries()) {
        QWidget *page = entry.plugin->createPage(nullptr);
        QVERIFY(page != nullptr);
        page->show();                  // offscreen 下不真正显示，但触发样式/布局初始化
        QApplication::processEvents();  // 跑一遍事件循环，验证无崩溃
        page->hide();
        delete page;                   // 跨 DLL 析构
    }
}

int main(int argc, char *argv[])
{
    // 无头环境跑 GUI 测试必须指定 offscreen 平台，且要在 QApplication 构造前设置。
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // 把 QSettings 引到临时 ini，避免测试读写污染真实注册表/配置。
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const QString iniDir = QDir::tempPath() + QStringLiteral("/toolbox_test_settings");
    QDir().mkpath(iniDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, iniDir);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, iniDir);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ToolBoxTest"));
    QApplication::setApplicationName(QStringLiteral("ToolBoxTest"));

    toolbox::Logger::install(iniDir + QStringLiteral("/toolbox_test.log"));

    return QTest::qExec(new TestIntegration, argc, argv);
}

#include "tst_integration.moc"
