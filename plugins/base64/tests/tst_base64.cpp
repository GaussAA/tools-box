#include "core/Base64Codec.h"

#include <QTest>

// Base64 编解码的纯逻辑。
//
// 这些规则原先写在页面的按钮 lambda 里，与界面混在一起无法单测；现在可直接
// 用任意字符串验证，重点是三处容易出错的地方：非 ASCII 的 UTF-8 往返、URL 安全
// 字符集的补位行为、粘贴文本首尾的空白。
class TestBase64Codec : public QObject
{
    Q_OBJECT

private slots:
    void encodesAscii();
    void encodesUtf8ForNonAscii();
    void encodesEmptyString();
    void urlSafeSwapsPlusAndSlash();
    void decodesAscii();
    void decodesUtf8BackToChinese();
    void decodeIgnoresSurroundingWhitespace();
    void decodeIsLenientAboutInvalidCharacters();
    void roundTripsBothCharsets();
};

void TestBase64Codec::encodesAscii()
{
    // 经典样例：Man → TWFu（RFC 4648 里的向量）。
    QCOMPARE(base64::encodeBase64(QStringLiteral("Man"), false), QStringLiteral("TWFu"));
    QCOMPARE(base64::encodeBase64(QStringLiteral("Ma"), false), QStringLiteral("TWE="));
    QCOMPARE(base64::encodeBase64(QStringLiteral("M"), false), QStringLiteral("TQ=="));
}

void TestBase64Codec::encodesUtf8ForNonAscii()
{
    // 「中文」的 UTF-8 字节是 E4 B8 AD E6 96 87，编出来必须等于按字节编的结果。
    const QString encoded = base64::encodeBase64(QStringLiteral("中文"), false);
    QCOMPARE(encoded, QString::fromLatin1(QStringLiteral("中文").toUtf8().toBase64()));
    QVERIFY(!encoded.contains(QChar(0xFFFD)));
}

void TestBase64Codec::encodesEmptyString()
{
    QCOMPARE(base64::encodeBase64(QString(), false), QString());
}

void TestBase64Codec::urlSafeSwapsPlusAndSlash()
{
    // 挑一个字节数不是 3 的倍数的输入：EF BE BF 61 里 0xFB/0xFF 在标准字符集里
    // 恰好编成 '+'，且结尾有一个 '=' 补位。
    const QString original = QString(QChar(0xFBFF)) + QStringLiteral("a");
    const QByteArray bytes = original.toUtf8();

    const QString standard = base64::encodeBase64(original, false);
    const QString urlSafe = base64::encodeBase64(original, true);

    // 与 Qt 自己的编解码结果对齐（我们只负责传对字符集选项）。
    QCOMPARE(standard, QString::fromLatin1(bytes.toBase64()));
    QCOMPARE(urlSafe, QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding)));

    // 字符集确实换了：+ 变 -，两种字符集都不会出现对方的符号。
    QVERIFY(standard.contains(QLatin1Char('+')));
    QVERIFY(!urlSafe.contains(QLatin1Char('+')) && !urlSafe.contains(QLatin1Char('/')));
    // 补位仍然保留 —— Qt 的 Base64UrlEncoding 只换字符集（这是本插件的既有行为，
    // 页面从前就是这么编的；要去补位得另加 OmitPadding）。
    QVERIFY(standard.endsWith(QLatin1Char('=')));
    QVERIFY(urlSafe.endsWith(QLatin1Char('=')));
}

void TestBase64Codec::decodesAscii()
{
    QCOMPARE(base64::decodeBase64(QStringLiteral("TWFu"), false), QStringLiteral("Man"));
}

void TestBase64Codec::decodesUtf8BackToChinese()
{
    const QString encoded = base64::encodeBase64(QStringLiteral("中文标题"), false);
    QCOMPARE(base64::decodeBase64(encoded, false), QStringLiteral("中文标题"));
}

void TestBase64Codec::decodeIgnoresSurroundingWhitespace()
{
    // 从记事本/聊天软件粘贴常带首尾空白与换行，不容忍就会解出空串。
    QCOMPARE(base64::decodeBase64(QStringLiteral("  TWFu\n"), false), QStringLiteral("Man"));
}

void TestBase64Codec::decodeIsLenientAboutInvalidCharacters()
{
    // Qt 的解码器是宽容的：非法字符被跳过，剩下的合法字符照样解。所以脏输入
    // 得到的不是空串，而是一堆替换字符。这条用例把「宽容」这个事实钉住 ——
    // 将来若改成「先校验字符集、非法即报错」，这条会红，提醒同步改界面文案。
    const QString decoded = base64::decodeBase64(QStringLiteral("!!!not base64!!!"), false);
    QVERIFY(!decoded.isEmpty());
    QVERIFY(decoded != QStringLiteral("!!!not base64!!!"));
}

void TestBase64Codec::roundTripsBothCharsets()
{
    const QString original = QStringLiteral("混合 text 与中文 123");
    QCOMPARE(base64::decodeBase64(base64::encodeBase64(original, false), false), original);
    QCOMPARE(base64::decodeBase64(base64::encodeBase64(original, true), true), original);
}

QTEST_APPLESS_MAIN(TestBase64Codec)

#include "tst_base64.moc"
