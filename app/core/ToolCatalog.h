#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

// 外壳里不依赖界面的那部分逻辑：导航的过滤规则、收藏/最近使用列表的维护。
//
// 放在这里而不是 MainWindow 里，是为了让规则能脱离 QListWidget 单测。
// 判定标准见 docs/architecture.md §3：凡是能用不依赖 QWidget 的方式表达的
// 逻辑，就必须放在不依赖 QWidget 的类里。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace toolbox {

/// 导航列表里一行的判定输入。
/// 外壳在每次过滤前按当前列表逐行填一份出来，交给纯函数去算可见性。
struct NavRow
{
    bool isHeader = false; ///< 分组标题行：不可选中，也不参与搜索匹配
    QString searchText;    ///< 预先拼好的搜索文本（名称 + 分类 + 说明 + id）
    QString toolId;        ///< 工具 id；首页与标题行为空
};

/// 过滤结果，逐行对应传入的 rows。
struct NavFilterResult
{
    QList<bool> visible;        ///< 每行是否可见，含标题行
    QStringList matchedToolIds; ///< 命中的工具 id，已按首次出现顺序去重
};

/// 按关键字过滤导航行。
///
/// 规则有两条：可选中的行按 searchText 做大小写不敏感的子串匹配；分组标题
/// 只在其下方还有可见行时才显示，否则会留下一行空标题。needle 为空即
/// 「不过滤」，此时全部可见。
///
/// 同一个工具会同时出现在收藏、最近使用、分类三个分区里，它们指向同一个
/// 页面，所以命中数按工具 id 去重，而不是按行数。
NavFilterResult filterNavRows(const QList<NavRow> &rows, const QString &needle);

/// 剔除 ids 里已经加载不到的工具（插件被删掉或改了 id 的情况）。
/// 就地修改，避免调用方漏掉返回值。
void pruneMissingIds(QStringList &ids, const QSet<QString> &aliveIds);

/// 把 toolId 提到 recent 最前面，并把列表裁剪到 limit 条。
/// 已经在最前面时不做任何改动并返回 false —— 调用方据此跳过无意义的重建。
/// limit 需为正数。
bool promoteRecent(QStringList &recent, const QString &toolId, int limit);

} // namespace toolbox
