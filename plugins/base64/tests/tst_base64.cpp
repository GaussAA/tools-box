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

    void checkedDecodeAcceptsValidInput();
    void checkedDecodeAcceptsUrlSafeAlphabet();
    void checkedDecodeRejectsEmptyInput();
    void checkedDecodeRejectsLengthNotMultipleOfFour();
    void checkedDecodeRejectsIllegalCharacters();
    void checkedDecodeRejectsPaddingBeforeTheEnd();
    void checkedDecodeStillToleratesSurroundingWhitespace();
    void checkedDecodeKeepsTextIdenticalToLenientDecode();
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

void TestBase64Codec::checkedDecodeAcceptsValidInput()
{
    const base64::DecodeResult result = base64::decodeBase64Checked(QStringLiteral("TWFu"), false);
    QVERIFY(result.ok);
    QVERIFY(result.error == base64::DecodeError::None);
    QCOMPARE(result.text, QStringLiteral("Man"));
}

void TestBase64Codec::checkedDecodeAcceptsUrlSafeAlphabet()
{
    // U+FBFF + 'a' 编出来必然含 '+' 或 '/'（URL 安全字符集里就是 '-' 或 '_'），
    // 先钉住这个前提，下面「换字符集校验就会被拒」的断言才有意义。
    const QString original = QString(QChar(0xFBFF)) + QStringLiteral("a");
    const QString urlSafe = base64::encodeBase64(original, true);
    QVERIFY(urlSafe.contains(QLatin1Char('-')) || urlSafe.contains(QLatin1Char('_')));

    const base64::DecodeResult okResult = base64::decodeBase64Checked(urlSafe, true);
    QVERIFY(okResult.ok);
    QCOMPARE(okResult.text, original);

    // 勾了「URL 安全」却拿标准字符集去校验，必须被拒 —— 共用一套校验的话，
    // 这里会静默解出另一串字节，是最难排查的那种错。
    const base64::DecodeResult wrongSet = base64::decodeBase64Checked(urlSafe, false);
    QVERIFY(!wrongSet.ok);
    QVERIFY(wrongSet.error == base64::DecodeError::BadCharacter);
}

void TestBase64Codec::checkedDecodeRejectsEmptyInput()
{
    const base64::DecodeResult blank = base64::decodeBase64Checked(QStringLiteral("   "), false);
    QVERIFY(!blank.ok);
    QVERIFY(blank.error == base64::DecodeError::Empty);
    QVERIFY(blank.text.isEmpty());
}

void TestBase64Codec::checkedDecodeRejectsLengthNotMultipleOfFour()
{
    // 少了最后一个字符：可能是粘贴时被截断，也可能压根就是原文而不是 Base64。
    // 两种都无法解码，理直气壮地告诉用户，而不是解出一半。
    const base64::DecodeResult result = base64::decodeBase64Checked(QStringLiteral("TWF"), false);
    QVERIFY(!result.ok);
    QVERIFY(result.error == base64::DecodeError::BadLength);
}

void TestBase64Codec::checkedDecodeRejectsIllegalCharacters()
{
    // 正是宽容解码会「解出一堆乱码」的那种输入：改之前界面上表现为结果莫名
    // 变成问号，用户只会以为是自己粘错了。现在必须被拦在门口。
    const base64::DecodeResult result =
        base64::decodeBase64Checked(QStringLiteral("!!!notbase64"), false);
    QVERIFY(!result.ok);
    QVERIFY(result.error == base64::DecodeError::BadCharacter);
    QVERIFY(result.text.isEmpty());
}

void TestBase64Codec::checkedDecodeRejectsPaddingBeforeTheEnd()
{
    // 补位 '=' 只该出现在末尾：出现在中间或开头都说明这不是 Base64。
    const base64::DecodeResult middle =
        base64::decodeBase64Checked(QStringLiteral("AB=CDEFG"), false);
    QVERIFY(!middle.ok);
    QVERIFY(middle.error == base64::DecodeError::BadCharacter);

    const base64::DecodeResult leading =
        base64::decodeBase64Checked(QStringLiteral("=AAAAAAA"), false);
    QVERIFY(!leading.ok);
    QVERIFY(leading.error == base64::DecodeError::BadCharacter);
}

void TestBase64Codec::checkedDecodeStillToleratesSurroundingWhitespace()
{
    // 加了校验不能把「粘贴带换行」这个既有容忍度弄丢。
    const base64::DecodeResult result =
        base64::decodeBase64Checked(QStringLiteral("  TWFu\n"), false);
    QVERIFY(result.ok);
    QCOMPARE(result.text, QStringLiteral("Man"));
}

void TestBase64Codec::checkedDecodeKeepsTextIdenticalToLenientDecode()
{
    // 校验只是加了一道门，解码本身没变：合法输入走两个入口必须得到同一个结果。
    const QString encoded = base64::encodeBase64(QStringLiteral("中文标题"), false);
    const base64::DecodeResult checked = base64::decodeBase64Checked(encoded, false);
    QVERIFY(checked.ok);
    QCOMPARE(checked.text, base64::decodeBase64(encoded, false));
}

QTEST_APPLESS_MAIN(TestBase64Codec)

#include "tst_base64.moc"
