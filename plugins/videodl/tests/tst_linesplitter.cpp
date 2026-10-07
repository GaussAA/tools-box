#include "core/LineSplitter.h"

#include <QTest>

// 子进程输出的按行切分。
//
// 重点是「按块到达」带来的三个经典错法：半行当整行、\r 尾巴没剥、最后一行
// 没有换行被丢弃。真实下载里这些症状是偶发的（偶尔解析错一行），极难靠
// 真机复现，所以在这里逐条钉死。
class TestLineSplitter : public QObject
{
    Q_OBJECT

private slots:
    void splitsPlainLines();
    void keepsPartialLineUntilNextChunk();
    void stripsCarriageReturn();
    void handlesCarriageReturnSplitAcrossChunks();
    void flushReturnsTrailingLineWithoutNewline();
    void flushOnEmptyBufferReturnsEmpty();
    void keepsBlankLines();
    void flushIsRepeatable();
};

void TestLineSplitter::splitsPlainLines()
{
    videodl::LineSplitter splitter;
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("first\nsecond\n"));
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0), QByteArrayLiteral("first"));
    QCOMPARE(lines.at(1), QByteArrayLiteral("second"));
}

void TestLineSplitter::keepsPartialLineUntilNextChunk()
{
    // 「[download] 50」与「.0%」分两块到达：只有拼齐了才算一行。
    videodl::LineSplitter splitter;
    QCOMPARE(splitter.append(QByteArrayLiteral("[download] 50")).size(), 0);
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral(".0%\n"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("[download] 50.0%"));
}

void TestLineSplitter::stripsCarriageReturn()
{
    // Windows 控制台输出是 \r\n：\r 必须去掉，否则每行尾部多一个字符。
    videodl::LineSplitter splitter;
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("ERROR: bad\r\n"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("ERROR: bad"));
}

void TestLineSplitter::handlesCarriageReturnSplitAcrossChunks()
{
    // 最容易写错的一种：\r 落在上一块末尾、\n 落在下一块开头。
    // 若在「看到 \r 就当行尾」就会把半行提前切出去。
    videodl::LineSplitter splitter;
    QCOMPARE(splitter.append(QByteArrayLiteral("done\r")).size(), 0);
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("\n"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("done"));
}

void TestLineSplitter::flushReturnsTrailingLineWithoutNewline()
{
    // 子进程退出时最后一行常常没有换行，不 flush 就永远丢了。
    videodl::LineSplitter splitter;
    splitter.append(QByteArrayLiteral("no trailing newline"));
    QCOMPARE(splitter.flush(), QByteArrayLiteral("no trailing newline"));
}

void TestLineSplitter::flushOnEmptyBufferReturnsEmpty()
{
    // 输出恰好以换行结束时 flush 必须是空，而不是把空缓冲区当成一行。
    videodl::LineSplitter splitter;
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("done\n"));
    QCOMPARE(lines.size(), 1);
    QVERIFY(splitter.flush().isEmpty());
}

void TestLineSplitter::keepsBlankLines()
{
    // 空行原样交出：要不要忽略由调用方判断（DownloadRunner 会跳过空行）。
    videodl::LineSplitter splitter;
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("a\n\nb\n"));
    QCOMPARE(lines.size(), 3);
    QCOMPARE(lines.at(1), QByteArray());
}

void TestLineSplitter::flushIsRepeatable()
{
    // flush 之后缓冲清空，可以继续用同一个实例接下一段输出。
    videodl::LineSplitter splitter;
    splitter.append(QByteArrayLiteral("first\ntrailing"));
    QCOMPARE(splitter.flush(), QByteArrayLiteral("trailing"));
    QVERIFY(splitter.flush().isEmpty());
    const QList<QByteArray> lines = splitter.append(QByteArrayLiteral("next\n"));
    QCOMPARE(lines.size(), 1);
    QCOMPARE(lines.at(0), QByteArrayLiteral("next"));
}

QTEST_APPLESS_MAIN(TestLineSplitter)

#include "tst_linesplitter.moc"
