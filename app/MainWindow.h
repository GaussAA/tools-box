#pragma once

#include <QHash>
#include <QIcon>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPoint;
class QStackedWidget;
class ToolRegistry;

/// 工具箱外壳：左侧导航 + 右侧页面容器。
///
/// 外壳本身不认识任何具体工具，它只做四件事：
///   1. 从 ToolRegistry 拿到插件列表；
///   2. 为每个插件创建一个页面塞进 QStackedWidget，并帮它存取配置；
///   3. 导航选中变化时切换页面，搜索框输入时过滤导航；
///   4. 维护「收藏」和「最近使用」两个快捷分区。
/// 新增工具不需要修改这个文件。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    /// 关闭前把各工具页面的状态存回去。
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onNavRowChanged(int row);
    void onSearchTextChanged(const QString &text);
    void onNavContextMenu(const QPoint &pos);
    void reloadTools();
    void showAbout();
    void clearFavorites();
    void clearRecent();

private:
    /// 一个已加载工具在外壳里需要的全部信息，用来重建导航。
    /// 插件本身只在 reloadTools() 期间访问，之后一律走这份缓存。
    struct ToolEntry
    {
        QString id;
        QString name;
        QString category;
        QString description;
        QString searchText;
        QIcon icon;
        int pageIndex = 0;
    };

    void buildUi();
    void buildMenus();
    void updateHomePage(int toolCount);

    /// 按 m_tools / m_favorites / m_recent 重新生成整个导航列表，
    /// 并尽量把选中行还原到 m_currentToolId 对应的工具上。
    void rebuildNav();
    void scheduleNavRebuild();

    /// 遍历所有工具页面，调用它们的 IToolPage::saveState()。
    void saveAllToolStates();

    void toggleFavorite(const QString &toolId);
    void noteRecent(const QString &toolId);

    /// 按关键字过滤导航，返回命中的工具数量（首页不计入，同名工具去重）。
    int applyFilter(const QString &needle);

    const ToolEntry *findTool(const QString &toolId) const;

    ToolRegistry *m_registry = nullptr;
    QAction *m_clearFavorites = nullptr; ///< 列表为空时置灰，见 rebuildNav()
    QAction *m_clearRecent = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_nav = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLabel *m_homePage = nullptr; ///< 外壳自带页，始终位于 QStackedWidget 的 0 号位

    QList<ToolEntry> m_tools;
    QHash<int, QString> m_toolIdByPage; ///< QStackedWidget 下标 → 工具 id
    QStringList m_favorites;            ///< 收藏的工具 id，按收藏顺序
    QStringList m_recent;               ///< 最近使用的工具 id，最新在前

    QString m_currentToolId;       ///< 当前选中工具，重建导航时用来还原选中行
    QString m_statusMessage;       ///< 未搜索时状态栏显示的默认文案
    bool m_applyingFilter = false; ///< 过滤/重建期间抑制选中行带来的副作用
    bool m_rebuildScheduled = false;
};
