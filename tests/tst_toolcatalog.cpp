#include "core/ToolCatalog.h"

#include <QTest>

// 外壳导航的过滤规则与收藏/最近使用列表的维护。
//
// 这些规则原先长在 MainWindow 里，只能靠点界面来验证；抽成纯函数之后就能
// 直接喂数据进来断言每一条边界（见 docs/architecture.md §3）。
class TestToolCatalog : public QObject
{
    Q_OBJECT

private slots:
    void filterWithEmptyNeedleShowsEveryRow();
    void filterMatchesSearchTextCaseInsensitively();
    void filterHidesHeaderWithoutVisibleChild();
    void filterDedupesMatchedToolIds();
    void pruneKeepsOrderAndDropsUnknownIds();
    void promoteRecentMovesToFrontAndTrims();
    void promoteRecentIsNoOpWhenAlreadyFirst();
};

namespace {

toolbox::NavRow headerRow(const QString &text)
{
    toolbox::NavRow row;
    row.isHeader = true;
    row.searchText = text;
    return row;
}

toolbox::NavRow toolRow(const QString &id, const QString &searchText)
{
    toolbox::NavRow row;
    row.toolId = id;
    row.searchText = searchText;
    return row;
}

} // namespace

void TestToolCatalog::filterWithEmptyNeedleShowsEveryRow()
{
    const QList<toolbox::NavRow> rows{
        toolRow(QString(), QStringLiteral("首页")),
        headerRow(QStringLiteral("收藏")),
        toolRow(QStringLiteral("media.video-download"), QStringLiteral("视频下载 媒体工具")),
    };

    const toolbox::NavFilterResult result = toolbox::filterNavRows(rows, QString());

    QVERIFY(result.visible == QList<bool>({true, true, true}));
    // 首页没有工具 id，不该被算成一个命中的工具。
    QCOMPARE(result.matchedToolIds, QStringList{QStringLiteral("media.video-download")});
}

void TestToolCatalog::filterMatchesSearchTextCaseInsensitively()
{
    const QList<toolbox::NavRow> rows{
        toolRow(QStringLiteral("media.video-download"), QStringLiteral("视频下载 媒体工具")),
        toolRow(QStringLiteral("dev.json-format"), QStringLiteral("JSON 格式化 开发辅助")),
    };

    const toolbox::NavFilterResult result = toolbox::filterNavRows(rows, QStringLiteral("json"));

    QVERIFY(result.visible == QList<bool>({false, true}));
    QCOMPARE(result.matchedToolIds, QStringList{QStringLiteral("dev.json-format")});
}

void TestToolCatalog::filterHidesHeaderWithoutVisibleChild()
{
    const QList<toolbox::NavRow> rows{
        headerRow(QStringLiteral("收藏")),
        toolRow(QStringLiteral("dev.json-format"), QStringLiteral("JSON 格式化")),
        headerRow(QStringLiteral("最近使用")),
        toolRow(QStringLiteral("media.video-download"), QStringLiteral("视频下载")),
        headerRow(QStringLiteral("媒体工具")),
    };

    const toolbox::NavFilterResult result = toolbox::filterNavRows(rows, QStringLiteral("json"));

    // 第一组有命中行 → 显示；第二组全被滤掉 → 连标题一起藏；第三组下面没有行 → 也藏。
    QVERIFY(result.visible == QList<bool>({true, true, false, false, false}));
    QCOMPARE(result.matchedToolIds, QStringList{QStringLiteral("dev.json-format")});
}

void TestToolCatalog::filterDedupesMatchedToolIds()
{
    // 同一个工具同时出现在两个分区里，指向同一个页面，只该算一次。
    const QList<toolbox::NavRow> rows{
        headerRow(QStringLiteral("收藏")),
        toolRow(QStringLiteral("media.video-download"), QStringLiteral("视频下载")),
        headerRow(QStringLiteral("媒体工具")),
        toolRow(QStringLiteral("media.video-download"), QStringLiteral("视频下载")),
    };

    const toolbox::NavFilterResult result = toolbox::filterNavRows(rows, QStringLiteral("视频"));

    QVERIFY(result.visible == QList<bool>({true, true, true, true}));
    QCOMPARE(result.matchedToolIds.size(), 1);
    QCOMPARE(result.matchedToolIds.first(), QStringLiteral("media.video-download"));
}

void TestToolCatalog::pruneKeepsOrderAndDropsUnknownIds()
{
    QStringList ids{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"),
                    QStringLiteral("d")};
    const QSet<QString> alive{QStringLiteral("b"), QStringLiteral("d"), QStringLiteral("x")};

    toolbox::pruneMissingIds(ids, alive);

    QCOMPARE(ids, QStringList({QStringLiteral("b"), QStringLiteral("d")}));
}

void TestToolCatalog::promoteRecentMovesToFrontAndTrims()
{
    QStringList recent{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
    QVERIFY(toolbox::promoteRecent(recent, QStringLiteral("c"), 3));
    QCOMPARE(recent, QStringList({QStringLiteral("c"), QStringLiteral("a"), QStringLiteral("b")}));

    // 新工具进来时，超出上限的那条要被挤掉，且同一个工具不会重复出现。
    QVERIFY(toolbox::promoteRecent(recent, QStringLiteral("d"), 3));
    QCOMPARE(recent, QStringList({QStringLiteral("d"), QStringLiteral("c"), QStringLiteral("a")}));
}

void TestToolCatalog::promoteRecentIsNoOpWhenAlreadyFirst()
{
    QStringList recent{QStringLiteral("a"), QStringLiteral("b")};

    QVERIFY(!toolbox::promoteRecent(recent, QStringLiteral("a"), 5));
    QCOMPARE(recent, QStringList({QStringLiteral("a"), QStringLiteral("b")}));
}

QTEST_APPLESS_MAIN(TestToolCatalog)

#include "tst_toolcatalog.moc"
