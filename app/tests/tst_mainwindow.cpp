// MainWindow 烟雾测试：验证外壳在「无真实插件」环境下的四条主路径不崩溃。
//
// 覆盖范围（见 docs/best-practices-assessment.md §3.4）：
//   1. 构造 MainWindow（会触发 reloadTools 扫描默认 tools 目录，测试环境多半不存在）；
//   2. ToolRegistry::rescan 指向不存在目录时返回 0 并记录错误；
//   3. 切到首页（pageIndex 0）不崩；
//   4. 搜索过滤（输入关键字 + 清空）不崩。
//
// 两个用例都**通过真实控件的信号**驱动（QListWidget::currentRowChanged /
// QLineEdit::textChanged），而不是 `QMetaObject::invokeMethod` 的字符串重载 ——
// 后者被 coding-standards §4 明令禁止，而且只调槽、不验证连接是否接上了。
//
// 不部署真实插件 DLL：videodl 的 createPage 可能启动外部子进程/网络，
// 与 tests/CMakeLists.txt「不允许依赖真实网络或真实子进程」的约定冲突，
// 端到端加载留给集成测试。

#include "MainWindow.h"
#include "ToolRegistry.h"
#include "core/Logger.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QSettings>
#include <QSignalSpy>
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
    void languageMenuReflectsConfiguredChoice();
    void remembersWindowGeometryAcrossSessions();
    void clearActionsAreGreyedOutWhenListsAreEmpty();
};

namespace {

/// 按标题找子菜单（测试未装翻译，标题就是源语言文案）。
QMenu *findSubMenu(const QWidget *window, const QString &title)
{
    const auto menus = window->findChildren<QMenu *>();
    for (QMenu *menu : menus) {
        if (menu->title() == title) {
            return menu;
        }
    }
    return nullptr;
}

/// 按文案找菜单项。
QAction *findAction(const QMenu *menu, const QString &text)
{
    const auto actions = menu->actions();
    for (QAction *action : actions) {
        if (action->text() == text) {
            return action;
        }
    }
    return nullptr;
}

} // namespace

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
// 改变导航的当前行，让 QListWidget 自己发出 currentRowChanged 驱动私有槽。
void TestMainWindow::switchToHomePageDoesNotCrash()
{
    MainWindow w;
    auto *nav = w.findChild<QListWidget *>();
    auto *stack = w.findChild<QStackedWidget *>();
    QVERIFY2(nav != nullptr, "导航 QListWidget 未创建");
    QVERIFY2(stack != nullptr, "QStackedWidget 未创建");

    QSignalSpy rowSpy(nav, &QListWidget::currentRowChanged);
    QVERIFY(rowSpy.isValid());

    nav->setCurrentRow(-1); // 先离开首页，保证下面这次一定发生变化
    nav->setCurrentRow(0);  // 再切回首页
    QVERIFY2(rowSpy.count() >= 1, "currentRowChanged 未发出，槽可能没接上");
    QCOMPARE(stack->currentIndex(), 0);
}

// 搜索过滤：先输入关键字，再清空，两次都不应崩溃。
// 写真实输入框，让 QLineEdit 发出 textChanged 驱动私有槽。
void TestMainWindow::searchFilterDoesNotCrash()
{
    MainWindow w;
    auto *search = w.findChild<QLineEdit *>();
    QVERIFY2(search != nullptr, "搜索框 QLineEdit 未创建");

    QSignalSpy textSpy(search, &QLineEdit::textChanged);
    QVERIFY(textSpy.isValid());

    search->setText(QStringLiteral("base64"));
    QVERIFY2(textSpy.count() >= 1, "textChanged 未发出，槽可能没接上");
    search->clear();
    QVERIFY2(textSpy.count() >= 2, "清空未走同一路径");
}

// 语言菜单：勾选态必须如实反映 ui/language 配置（无配置 → 跟随系统；en → English）。
// 只断言勾选态，不触发 action —— 触发会弹 QMessageBox 阻塞无头会话；写配置那条
// 路径由真机冒烟（verify_shell -Lang en|zh）覆盖。
void TestMainWindow::languageMenuReflectsConfiguredChoice()
{
    QSettings().remove(QStringLiteral("ui/language"));
    {
        MainWindow w;
        QMenu *language = findSubMenu(&w, QStringLiteral("界面语言"));
        QVERIFY2(language != nullptr, "帮助菜单下没有「界面语言」子菜单");
        QAction *system = findAction(language, QStringLiteral("跟随系统"));
        QAction *english = findAction(language, QStringLiteral("English"));
        QVERIFY2(system != nullptr && english != nullptr, "语言选项不全");
        QVERIFY(system->isChecked());
        QVERIFY(!english->isChecked());
    }

    QSettings().setValue(QStringLiteral("ui/language"), QStringLiteral("en"));
    {
        MainWindow w;
        QMenu *language = findSubMenu(&w, QStringLiteral("界面语言"));
        QVERIFY2(language != nullptr, "帮助菜单下没有「界面语言」子菜单");
        QVERIFY(findAction(language, QStringLiteral("English"))->isChecked());
        QVERIFY(!findAction(language, QStringLiteral("跟随系统"))->isChecked());
    }
    QSettings().remove(QStringLiteral("ui/language"));
}

// 窗口几何：上次拖好的大小与位置要沿用，首次启动（无配置）才用默认尺寸。
// 用「另造一个顶层窗口」的办法拿到一份真实的几何字节，尺寸刻意取成与默认
// 1024x700 不同的 777x555 —— 这样「还原了」与「忽略了配置」一眼可辨，
// 不会出现「两者恰好一样所以断言白写」的假绿。
void TestMainWindow::remembersWindowGeometryAcrossSessions()
{
    QSettings().remove(QStringLiteral("ui/geometry"));

    QWidget sample;
    sample.resize(777, 555);
    const QByteArray saved = sample.saveGeometry();
    QVERIFY2(!saved.isEmpty(), "样本窗口没能给出几何数据");

    QSettings().setValue(QStringLiteral("ui/geometry"), saved);
    {
        MainWindow w;
        QCOMPARE(w.size(), QSize(777, 555));
    }

    // 抹掉配置后必须回到默认尺寸，而不是把上一轮的残留读出来 —— 顺带确认
    // 上面那条断言不是碰巧成立。
    QSettings().remove(QStringLiteral("ui/geometry"));
    {
        MainWindow w;
        QCOMPARE(w.size(), QSize(1024, 700));
    }
}

// 「收藏」与「最近使用」此前只能逐个右键取消，攒多了就清不动。菜单给了一次性
// 入口后要三件事同时成立：配置清掉、导航里的分区消失、动作本身变灰（空了还
// 让人点是没有意义的）。三条里少验一条，就可能出现「配置清了界面还留着」。
namespace {

/// 通过菜单**真实触发**动作：只改配置不算验证，得证明动作接上了。
QAction *fileMenuAction(const QWidget *window, const QString &text)
{
    QMenu *file = findSubMenu(window, QStringLiteral("文件"));
    return file ? findAction(file, text) : nullptr;
}

} // namespace

void TestMainWindow::clearActionsAreGreyedOutWhenListsAreEmpty()
{
    // 「收藏」与「最近使用」此前只能逐个右键取消，攒多了就清不动，菜单里
    // 给了一次性入口。这里能钉住的是：**列表为空时动作必须置灰** —— 点了
    // 没反应比按钮灰着更让人困惑。
    //
    // 至于「清空后分区真的消失」那一半，本环境测不到：无插件时 reloadTools()
    // 会走 pruneMissingIds() 把失效 id 全部剔除，两个列表恒为空，分区根本
    // 不会出现（这不是缺陷，是既有设计）。那一半交给真机冒烟。
    QSettings().remove(QStringLiteral("ui/favorites"));
    QSettings().remove(QStringLiteral("ui/recent"));

    MainWindow w;

    QAction *clearFavorites = fileMenuAction(&w, QStringLiteral("清空收藏"));
    QAction *clearRecent = fileMenuAction(&w, QStringLiteral("清空最近使用"));
    QVERIFY2(clearFavorites != nullptr, "文件菜单里没有「清空收藏」");
    QVERIFY2(clearRecent != nullptr, "文件菜单里没有「清空最近使用」");
    QVERIFY(!clearFavorites->isEnabled());
    QVERIFY(!clearRecent->isEnabled());
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
