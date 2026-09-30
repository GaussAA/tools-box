#include "MainWindow.h"

#include "ToolBoxPlugin.h"
#include "ToolRegistry.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QSet>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

constexpr int kNavPanelWidth = 232;
constexpr int kNavIconSize = 18;

/// 「最近使用」分区最多保留几个工具。
constexpr int kRecentLimit = 6;

/// 导航项把对应的 QStackedWidget 下标存在这个角色里。
/// 分组标题行没有这个角色，据此就能把标题和可选中的项区分开。
constexpr int kPageIndexRole = Qt::UserRole;

/// 预先拼好的搜索文本（名称 + 分类 + 说明 + id），避免每次按键重新拼串。
constexpr int kSearchTextRole = Qt::UserRole + 1;

/// 工具 id。同一个工具会出现在收藏、最近使用、分类三个分区里，
/// 靠这个角色就能认出它们是同一个页面。首页没有这个角色。
constexpr int kToolIdRole = Qt::UserRole + 2;

// 存的是工具 id 而不是显示名：显示名会随插件改名而失效，id 是稳定的。
const QString kLastToolKey = QStringLiteral("ui/lastToolId");
const QString kFavoritesKey = QStringLiteral("ui/favorites");
const QString kRecentKey = QStringLiteral("ui/recent");

/// 插件没提供图标时，用工具名首字符画一个圆角占位图标。
/// 色相由名字哈希决定，保证同一个工具每次启动颜色都一样。
QIcon fallbackToolIcon(const QString &name)
{
    constexpr int kSize = 48;

    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromHsv(static_cast<int>(qHash(name) % 360u), 140, 200));
    painter.drawRoundedRect(QRectF(2, 2, kSize - 4, kSize - 4), 12, 12);

    QFont font = painter.font();
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(),
                     Qt::AlignCenter,
                     name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());

    return QIcon(pixmap);
}

/// 剔除 ids 里已经加载不到的工具（插件被删掉或改了 id 的情况）。
void pruneMissingIds(QStringList &ids, const QSet<QString> &aliveIds)
{
    for (int i = ids.size() - 1; i >= 0; --i) {
        if (!aliveIds.contains(ids.at(i))) {
            ids.removeAt(i);
        }
    }
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_registry(new ToolRegistry(this))
{
    setWindowTitle(QStringLiteral("工具箱"));
    resize(1024, 700);

    // 收藏和最近使用必须在 reloadTools() 之前读回来：reloadTools() 会把
    // 内存里的这两个列表原样写回配置（用来剔除失效的工具 id），
    // 若此时还是空的，就把上次会话存的记录覆盖掉了。
    m_favorites = QSettings().value(kFavoritesKey).toStringList();
    m_recent = QSettings().value(kRecentKey).toStringList();

    buildUi();
    buildMenus();
    reloadTools();
}

void MainWindow::buildUi()
{
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(tr("搜索工具…"));
    m_search->setClearButtonEnabled(true);

    m_nav = new QListWidget(this);
    m_nav->setFrameShape(QFrame::NoFrame);
    m_nav->setIconSize(QSize(kNavIconSize, kNavIconSize));
    m_nav->setContextMenuPolicy(Qt::CustomContextMenu);

    // 搜索框和导航同处一个固定宽的侧栏里，这样搜索框能跟着侧栏一起对齐。
    auto *navPanel = new QWidget(this);
    navPanel->setFixedWidth(kNavPanelWidth);
    auto *navLayout = new QVBoxLayout(navPanel);
    navLayout->setContentsMargins(8, 8, 4, 8);
    navLayout->setSpacing(8);
    navLayout->addWidget(m_search);
    navLayout->addWidget(m_nav, 1);

    m_stack = new QStackedWidget(this);

    m_homePage = new QLabel(m_stack);
    m_homePage->setAlignment(Qt::AlignCenter);
    m_homePage->setWordWrap(true);
    m_homePage->setMargin(40);
    m_stack->addWidget(m_homePage);

    auto *central = new QWidget(this);
    auto *layout = new QHBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(navPanel);
    layout->addWidget(m_stack, 1);
    setCentralWidget(central);

    connect(m_nav, &QListWidget::currentRowChanged, this, &MainWindow::onNavRowChanged);
    connect(m_nav, &QListWidget::customContextMenuRequested, this, &MainWindow::onNavContextMenu);
    connect(m_search, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
}

void MainWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("文件"));
    fileMenu->addAction(tr("重载插件"), QKeySequence(Qt::Key_F5), this, &MainWindow::reloadTools);
    fileMenu->addAction(tr("打开插件目录"), this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(ToolRegistry::defaultPluginDir()));
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("退出"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *helpMenu = menuBar()->addMenu(tr("帮助"));
    helpMenu->addAction(tr("关于"), this, &MainWindow::showAbout);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveAllToolStates();
    QMainWindow::closeEvent(event);
}

void MainWindow::reloadTools()
{
    // 顺序很重要：先让每个页面把状态存好，再销毁页面，最后才卸载插件 DLL。
    // 插件一旦被卸载，页面里的虚表指针就悬空了。
    saveAllToolStates();

    m_stack->removeWidget(m_homePage);
    while (m_stack->count() > 0) {
        QWidget *page = m_stack->widget(0);
        m_stack->removeWidget(page);
        delete page;
    }
    m_stack->addWidget(m_homePage);

    m_tools.clear();
    m_toolIdByPage.clear();

    const int toolCount = m_registry->rescan();

    for (const ToolRegistry::Entry &entry : m_registry->entries()) {
        const toolbox::ToolMeta meta = entry.plugin->meta();
        const QString category = meta.category.isEmpty() ? tr("其他") : meta.category;

        QWidget *page = entry.plugin->createPage(m_stack);
        const int pageIndex = m_stack->addWidget(page);
        m_toolIdByPage.insert(pageIndex, meta.id);

        // 页面如果实现了 IToolPage，就把上次保存的配置还给它。
        if (auto *toolPage = qobject_cast<toolbox::IToolPage *>(page)) {
            toolPage->restoreState(toolbox::ToolSettings(meta.id));
        }

        ToolEntry tool;
        tool.id = meta.id;
        tool.name = meta.name;
        tool.category = category;
        tool.description = meta.description;
        tool.searchText = QStringList{meta.name, category, meta.description, meta.id}
                              .join(QLatin1Char(' '));
        tool.icon = meta.icon;
        tool.pageIndex = pageIndex;
        m_tools.append(tool);
    }

    // 插件被删掉之后，收藏和最近使用里可能残留已经不存在的 id，一并清掉。
    QSet<QString> aliveIds;
    for (const ToolEntry &tool : m_tools) {
        aliveIds.insert(tool.id);
    }
    pruneMissingIds(m_favorites, aliveIds);
    pruneMissingIds(m_recent, aliveIds);
    QSettings().setValue(kFavoritesKey, m_favorites);
    QSettings().setValue(kRecentKey, m_recent);

    updateHomePage(toolCount);

    m_statusMessage = tr("已加载 %1 个工具 · 插件目录 %2")
                          .arg(toolCount)
                          .arg(QDir::toNativeSeparators(ToolRegistry::defaultPluginDir()));

    // 重载后回到完整列表视图。m_search 本来就是空的时候 clear() 不发信号，
    // 所以过滤交给 rebuildNav() 末尾统一做。
    m_search->clear();
    m_currentToolId = QSettings().value(kLastToolKey).toString();
    rebuildNav();
    statusBar()->showMessage(m_statusMessage);
}

const MainWindow::ToolEntry *MainWindow::findTool(const QString &toolId) const
{
    for (const ToolEntry &tool : m_tools) {
        if (tool.id == toolId) {
            return &tool;
        }
    }
    return nullptr;
}

void MainWindow::rebuildNav()
{
    const bool wasApplying = m_applyingFilter;
    m_applyingFilter = true;

    m_nav->clear();

    auto addHeader = [this](const QString &text) {
        auto *header = new QListWidgetItem(text, m_nav);
        // 分组标题只是视觉分隔。NoItemFlags 让鼠标点击不会改变当前行，
        // 所以它不会触发 onNavRowChanged，也就不会打乱页面映射。
        header->setFlags(Qt::NoItemFlags);
        QFont headerFont = header->font();
        if (headerFont.pointSizeF() > 0) {
            headerFont.setPointSizeF(headerFont.pointSizeF() * 0.85);
        }
        header->setFont(headerFont);
        header->setForeground(palette().color(QPalette::PlaceholderText));
    };

    auto addTool = [this](const ToolEntry &tool) {
        auto *item = new QListWidgetItem(tool.name, m_nav);
        item->setIcon(tool.icon.isNull() ? fallbackToolIcon(tool.name) : tool.icon);
        item->setData(kPageIndexRole, tool.pageIndex);
        item->setData(kToolIdRole, tool.id);
        item->setData(kSearchTextRole, tool.searchText);
        item->setToolTip(tool.description.isEmpty() ? tool.name : tool.description);
    };

    auto *homeItem = new QListWidgetItem(tr("首页"), m_nav);
    homeItem->setData(kPageIndexRole, 0);
    homeItem->setData(kSearchTextRole, tr("首页"));

    // 同一个工具会同时出现在多个分区里，这是刻意的：它们指向同一个
    // QStackedWidget 页面，选中哪一行都只是切到同一个页面。
    if (!m_favorites.isEmpty()) {
        addHeader(tr("收藏"));
        for (const QString &id : m_favorites) {
            if (const ToolEntry *tool = findTool(id)) {
                addTool(*tool);
            }
        }
    }

    if (!m_recent.isEmpty()) {
        addHeader(tr("最近使用"));
        for (const QString &id : m_recent) {
            if (const ToolEntry *tool = findTool(id)) {
                addTool(*tool);
            }
        }
    }

    QString currentCategory;
    for (const ToolEntry &tool : m_tools) {
        if (tool.category != currentCategory) {
            currentCategory = tool.category;
            addHeader(currentCategory);
        }
        addTool(tool);
    }

    // 还原选中行。m_currentToolId 找不到（首次启动或工具已消失）就回到首页。
    int row = -1;
    if (!m_currentToolId.isEmpty()) {
        for (int i = 0; i < m_nav->count(); ++i) {
            if (m_nav->item(i)->data(kToolIdRole).toString() == m_currentToolId) {
                row = i;
                break;
            }
        }
    }
    if (row < 0) {
        row = 0;
        m_currentToolId.clear();
    }
    m_nav->setCurrentRow(row);

    m_applyingFilter = wasApplying;

    // 导航项全换了，之前算出来的隐藏状态要重算一遍。
    applyFilter(m_search->text().trimmed());
}

void MainWindow::scheduleNavRebuild()
{
    if (m_rebuildScheduled) {
        return;
    }
    m_rebuildScheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_rebuildScheduled = false;
        rebuildNav();
    });
}

int MainWindow::applyFilter(const QString &needle)
{
    const bool wasApplying = m_applyingFilter;
    m_applyingFilter = true;

    // 同一工具在多处分区出现，命中数按工具去重。
    QSet<QString> matchedIds;

    // 第一遍：判定每个可选中的项（首页 + 工具项）是否命中。
    for (int row = 0; row < m_nav->count(); ++row) {
        QListWidgetItem *item = m_nav->item(row);
        if (!item->data(kPageIndexRole).isValid()) {
            continue; // 分组标题留到第二遍统一处理
        }

        const QString haystack = item->data(kSearchTextRole).toString();
        const bool hit = needle.isEmpty() || haystack.contains(needle, Qt::CaseInsensitive);
        item->setHidden(!hit);

        // 首页没有工具 id，不计入工具数。
        const QString toolId = item->data(kToolIdRole).toString();
        if (hit && !toolId.isEmpty()) {
            matchedIds.insert(toolId);
        }
    }

    // 第二遍：分组标题只在它下面还有可见工具时才显示，否则就是一行空标题。
    for (int row = 0; row < m_nav->count(); ++row) {
        QListWidgetItem *header = m_nav->item(row);
        if (header->data(kPageIndexRole).isValid()) {
            continue;
        }

        bool hasVisibleChild = false;
        for (int next = row + 1; next < m_nav->count(); ++next) {
            QListWidgetItem *candidate = m_nav->item(next);
            if (!candidate->data(kPageIndexRole).isValid()) {
                break; // 遇到下一个分组标题，本组结束
            }
            if (!candidate->isHidden()) {
                hasVisibleChild = true;
                break;
            }
        }
        header->setHidden(!hasVisibleChild);
    }

    // 搜索时直接把当前行让给第一个命中项，省掉一次点击。
    if (!needle.isEmpty()) {
        QListWidgetItem *current = m_nav->currentItem();
        if (!current || current->isHidden()) {
            for (int row = 0; row < m_nav->count(); ++row) {
                QListWidgetItem *item = m_nav->item(row);
                if (!item->isHidden() && item->data(kPageIndexRole).isValid()) {
                    m_nav->setCurrentRow(row);
                    break;
                }
            }
        }
    }

    m_applyingFilter = wasApplying;
    return matchedIds.size();
}

void MainWindow::onSearchTextChanged(const QString &text)
{
    const QString needle = text.trimmed();
    const int matchCount = applyFilter(needle);

    if (needle.isEmpty()) {
        statusBar()->showMessage(m_statusMessage);
    } else if (matchCount == 0) {
        statusBar()->showMessage(tr("没有匹配「%1」的工具").arg(needle));
    } else {
        statusBar()->showMessage(tr("匹配到 %1 个工具").arg(matchCount));
    }
}

void MainWindow::onNavRowChanged(int row)
{
    QListWidgetItem *item = m_nav->item(row);
    if (!item) {
        return;
    }

    const QVariant pageIndex = item->data(kPageIndexRole);
    if (!pageIndex.isValid()) {
        return; // 分组标题行，保持当前页面不变
    }

    m_stack->setCurrentIndex(pageIndex.toInt());

    // 过滤或重建导航时，选中行是程序自己改的，不该覆盖用户的选择。
    if (m_applyingFilter) {
        return;
    }

    const QString toolId = item->data(kToolIdRole).toString();
    m_currentToolId = toolId;
    QSettings().setValue(kLastToolKey, toolId);

    if (!toolId.isEmpty()) {
        noteRecent(toolId);
    }
}

void MainWindow::noteRecent(const QString &toolId)
{
    // 已经在最前面就不用动，免得每次切换都白重建一次导航。
    if (!m_recent.isEmpty() && m_recent.first() == toolId) {
        return;
    }

    m_recent.removeAll(toolId);
    m_recent.prepend(toolId);
    while (m_recent.size() > kRecentLimit) {
        m_recent.removeLast();
    }
    QSettings().setValue(kRecentKey, m_recent);

    // 「最近使用」的顺序变了，导航要跟着重建。但此刻正在处理
    // currentRowChanged，直接重建会重入，所以推到事件循环的下一轮。
    scheduleNavRebuild();
}

void MainWindow::toggleFavorite(const QString &toolId)
{
    if (m_favorites.removeAll(toolId) == 0) {
        m_favorites.append(toolId);
    }
    QSettings().setValue(kFavoritesKey, m_favorites);

    // 收藏分区会增删，导航结构直接重建。rebuildNav 会把选中行还原回
    // m_currentToolId，所以用户视觉上不会跳到别处。
    rebuildNav();
}

void MainWindow::onNavContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_nav->itemAt(pos);
    if (!item) {
        return;
    }

    const QString toolId = item->data(kToolIdRole).toString();
    if (toolId.isEmpty()) {
        return; // 首页和分组标题没有工具 id
    }

    QMenu menu(this);
    QAction *toggle =
        menu.addAction(m_favorites.contains(toolId) ? tr("取消收藏") : tr("收藏"));
    if (menu.exec(m_nav->viewport()->mapToGlobal(pos)) == toggle) {
        toggleFavorite(toolId);
    }
}

void MainWindow::saveAllToolStates()
{
    for (const ToolEntry &tool : m_tools) {
        QWidget *page = m_stack->widget(tool.pageIndex);
        if (auto *toolPage = qobject_cast<toolbox::IToolPage *>(page)) {
            toolPage->saveState(toolbox::ToolSettings(tool.id));
        }
    }
}

void MainWindow::updateHomePage(int toolCount)
{
    const QString pluginDir =
        QDir::toNativeSeparators(ToolRegistry::defaultPluginDir()).toHtmlEscaped();

    QString text = QStringLiteral("<h3>%1</h3>").arg(tr("工具箱"));
    text += QStringLiteral("<p>%1</p>").arg(tr("已加载 <b>%1</b> 个工具。").arg(toolCount));
    text += QStringLiteral("<p>%1</p>").arg(tr("在左侧右键任意工具，可以把它加进「收藏」。"));
    text += QStringLiteral("<p>%1</p>")
                .arg(tr("要收录新工具，把插件 DLL 放进下面的目录，再按 F5 重新加载：<br>"
                        "<code>%1</code>")
                         .arg(pluginDir));

    const QStringList errors = m_registry->errors();
    if (!errors.isEmpty()) {
        QString list;
        for (const QString &error : errors) {
            list += QStringLiteral("<li>%1</li>").arg(error.toHtmlEscaped());
        }
        text += QStringLiteral("<p style='color:#b91c1c;'>%1</p><ul>%2</ul>")
                    .arg(tr("以下插件加载失败："), list);
    }

    m_homePage->setText(text);
}

void MainWindow::showAbout()
{
    QMessageBox::about(this,
                       tr("关于工具箱"),
                       tr("<b>工具箱</b> %1<br><br>"
                          "基于 Qt %2 构建的插件式桌面工具箱。<br>"
                          "每个工具都是一个独立 DLL 插件，放进 tools 目录即可生效。")
                           .arg(QApplication::applicationVersion(), QStringLiteral(QT_VERSION_STR)));
}