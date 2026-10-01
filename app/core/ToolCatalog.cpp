#include "ToolCatalog.h"

namespace toolbox {

NavFilterResult filterNavRows(const QList<NavRow> &rows, const QString &needle)
{
    NavFilterResult result;
    result.visible.resize(rows.size());

    // 第一遍：判定每个可选中的行是否命中。标题行全部留到第二遍处理，
    // 因为它是否可见取决于「下面还有没有可见行」，必须等这一遍算完。
    for (int row = 0; row < rows.size(); ++row) {
        const NavRow &entry = rows.at(row);
        if (entry.isHeader) {
            continue;
        }

        const bool hit = needle.isEmpty() || entry.searchText.contains(needle, Qt::CaseInsensitive);
        result.visible[row] = hit;

        // 首页没有工具 id，不计入工具数。
        if (hit && !entry.toolId.isEmpty() && !result.matchedToolIds.contains(entry.toolId)) {
            result.matchedToolIds.append(entry.toolId);
        }
    }

    // 第二遍：标题行只在它下面还有可见行时才显示，遇到下一个标题行为止。
    for (int row = 0; row < rows.size(); ++row) {
        if (!rows.at(row).isHeader) {
            continue;
        }

        bool hasVisibleChild = false;
        for (int next = row + 1; next < rows.size(); ++next) {
            if (rows.at(next).isHeader) {
                break; // 遇到下一个分组标题，本组结束
            }
            if (result.visible.at(next)) {
                hasVisibleChild = true;
                break;
            }
        }
        result.visible[row] = hasVisibleChild;
    }

    return result;
}

void pruneMissingIds(QStringList &ids, const QSet<QString> &aliveIds)
{
    // 倒着删，下标才不会因为前面的元素被移除而错位。
    for (int i = ids.size() - 1; i >= 0; --i) {
        if (!aliveIds.contains(ids.at(i))) {
            ids.removeAt(i);
        }
    }
}

bool promoteRecent(QStringList &recent, const QString &toolId, int limit)
{
    if (!recent.isEmpty() && recent.first() == toolId) {
        return false;
    }

    // 先删掉旧位置再插到最前：这样「同一个工具只出现一次」由这段逻辑保证，
    // 调用方不需要自己判重。
    recent.removeAll(toolId);
    recent.prepend(toolId);
    while (recent.size() > limit) {
        recent.removeLast();
    }
    return true;
}

} // namespace toolbox
