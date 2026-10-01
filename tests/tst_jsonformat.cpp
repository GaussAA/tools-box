#include "core/JsonFormat.h"

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

QTEST_APPLESS_MAIN(TestJsonFormat)

#include "tst_jsonformat.moc"
