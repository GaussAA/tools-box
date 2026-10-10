#include "core/JsonFormat.h"

#include <QStringList>
#include <QTest>

// JSON 格式化插件的纯逻辑（plugins/jsonfmt/core）用例。
//
// 这段逻辑以前写在页面的 lambda 里 —— 界面上点得出来，但测不到；抽到 core/
// 之后「输入一段文本、得到什么输出」就可以直接断言（见 docs/architecture.md §3）。
class TestJsonFormat : public QObject
{
    Q_OBJECT

private slots:
    void formatJsonIndentsValidInput();
    void formatJsonCompactsValidInput();
    void formatJsonReportsErrorOffset();
    void formatJsonRejectsEmptyInput();

    void indentUnitMatchesEachStyle();
    void indentTwoSpacesUsesTwoSpaceSteps();
    void indentTabUsesTabs();
    void indentIsOnlyCosmetic();
    void locateErrorOnTheFirstLine();
    void locateErrorCountsEarlierNewlines();
    void locateErrorCountsCharactersNotBytesForChinese();
    void locateErrorRejectsNegativeOffset();
};

void TestJsonFormat::formatJsonIndentsValidInput()
{
    const jsonfmt::FormatResult indented = jsonfmt::formatJson(QStringLiteral("{\"a\":1}"), false);

    QVERIFY(indented.ok);
    QVERIFY(indented.text.contains(QLatin1Char('\n'))); // 缩进版是多行的
    QVERIFY(indented.errorOffset < 0);
    QVERIFY(indented.errorText.isEmpty());

    // 往返一致：缩进后的结果压缩回去应当回到原样，否则就是格式化改了数据。
    const jsonfmt::FormatResult back = jsonfmt::formatJson(indented.text, true);
    QVERIFY(back.ok);
    QCOMPARE(back.text, QStringLiteral("{\"a\":1}"));
}

void TestJsonFormat::formatJsonCompactsValidInput()
{
    const jsonfmt::FormatResult compacted =
        jsonfmt::formatJson(QStringLiteral("{ \"a\" : [ 1, 2 ] }"), true);

    QVERIFY(compacted.ok);
    QVERIFY(!compacted.text.contains(QLatin1Char('\n')));
    QCOMPARE(compacted.text, QStringLiteral("{\"a\":[1,2]}"));
}

void TestJsonFormat::formatJsonReportsErrorOffset()
{
    // 位置与原因都要回传：只说「解析失败」对定位问题毫无帮助，页面那句提示
    // 之所以能指出偏移，靠的就是 core 把 offset 带出来。
    const jsonfmt::FormatResult result = jsonfmt::formatJson(QStringLiteral("{\"a\": }"), true);

    QVERIFY(!result.ok);
    QVERIFY(result.text.isEmpty());
    QVERIFY(result.errorOffset >= 0);
    QVERIFY(!result.errorText.isEmpty());
}

void TestJsonFormat::formatJsonRejectsEmptyInput()
{
    // 空输入不是合法 JSON，也不该被当成「成功输出空串」。
    const jsonfmt::FormatResult result = jsonfmt::formatJson(QString(), true);

    QVERIFY(!result.ok);
    QVERIFY(result.text.isEmpty());
}

void TestJsonFormat::indentUnitMatchesEachStyle()
{
    QCOMPARE(jsonfmt::indentUnit(jsonfmt::Indent::TwoSpaces), QStringLiteral("  "));
    QCOMPARE(jsonfmt::indentUnit(jsonfmt::Indent::FourSpaces), QStringLiteral("    "));
    QCOMPARE(jsonfmt::indentUnit(jsonfmt::Indent::Tab), QStringLiteral("\t"));
}

void TestJsonFormat::indentTwoSpacesUsesTwoSpaceSteps()
{
    // Qt 只会给 4 空格，2 空格是我们自己换算出来的：第一级 2 个、第二级 4 个。
    const jsonfmt::FormatResult result =
        jsonfmt::formatJson(QStringLiteral("{\"a\":{\"b\":1}}"), false, jsonfmt::Indent::TwoSpaces);
    QVERIFY(result.ok);

    const QStringList lines = result.text.split(QLatin1Char('\n'));
    QVERIFY(lines.count() >= 3);
    QVERIFY(lines.at(1).startsWith(QStringLiteral("  \"")));
    // 是 2 不是 3：多一个空格就说明换算错了。
    QVERIFY(!lines.at(1).startsWith(QStringLiteral("   \"")));
    QVERIFY(lines.at(2).startsWith(QStringLiteral("    \"")));
}

void TestJsonFormat::indentTabUsesTabs()
{
    const jsonfmt::FormatResult result =
        jsonfmt::formatJson(QStringLiteral("{\"a\":{\"b\":1}}"), false, jsonfmt::Indent::Tab);
    QVERIFY(result.ok);

    const QStringList lines = result.text.split(QLatin1Char('\n'));
    QVERIFY(lines.at(1).startsWith(QStringLiteral("\t\"")));
    QVERIFY(lines.at(2).startsWith(QStringLiteral("\t\t\"")));
    // 换成了 Tab 就不该再留着行首空格。
    QVERIFY(!result.text.contains(QStringLiteral("\n ")));
}

void TestJsonFormat::indentIsOnlyCosmetic()
{
    // 换缩进只是排版：三种样式压缩回去都必须回到同一个串，不能改数据。
    const jsonfmt::Indent styles[] = {jsonfmt::Indent::TwoSpaces, jsonfmt::Indent::FourSpaces,
                                      jsonfmt::Indent::Tab};
    for (const jsonfmt::Indent style : styles) {
        const jsonfmt::FormatResult formatted = jsonfmt::formatJson(
            QStringLiteral("{\"a\":[1,2],\"b\":{\"c\":\"中文\"}}"), false, style);
        QVERIFY(formatted.ok);
        const jsonfmt::FormatResult back = jsonfmt::formatJson(formatted.text, true);
        QVERIFY(back.ok);
        QCOMPARE(back.text, QStringLiteral("{\"a\":[1,2],\"b\":{\"c\":\"中文\"}}"));
    }
}

void TestJsonFormat::locateErrorOnTheFirstLine()
{
    // 没有换行时行号必须是 1，列从 1 起算。
    const jsonfmt::ErrorLocation at = jsonfmt::locateError(QStringLiteral("{\"a\": }"), 6);
    QVERIFY(at.line == 1);
    QVERIFY(at.column == 7);
}

void TestJsonFormat::locateErrorCountsEarlierNewlines()
{
    // 索引：a0 a1 a2 \n3 b4 b5 b6 \n7 c8 —— 偏移 8 落在第三行第 1 列。
    const jsonfmt::ErrorLocation at = jsonfmt::locateError(QStringLiteral("aaa\nbbb\nccc"), 8);
    QVERIFY(at.line == 3);
    QVERIFY(at.column == 1);
}

void TestJsonFormat::locateErrorCountsCharactersNotBytesForChinese()
{
    // 「中」「文」各占 3 字节：按字节数会得出第 12 列，按字符数才是第 8 列。
    // 用户数的是字符，不是字节 —— 这条把「先按 UTF-8 截断再数」钉住。
    const jsonfmt::ErrorLocation at = jsonfmt::locateError(QStringLiteral("{\"中文\": }"), 11);
    QVERIFY(at.line == 1);
    QVERIFY(at.column == 8);
}

void TestJsonFormat::locateErrorRejectsNegativeOffset()
{
    // 拿不到位置就如实说拿不到，界面据此退回「只报原因」。
    const jsonfmt::ErrorLocation at = jsonfmt::locateError(QStringLiteral("{}"), -1);
    QVERIFY(at.line == -1);
    QVERIFY(at.column == -1);
}

QTEST_APPLESS_MAIN(TestJsonFormat)

#include "tst_jsonformat.moc"
