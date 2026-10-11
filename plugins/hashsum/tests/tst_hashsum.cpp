#include "core/HashSum.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

// 哈希与校验和插件的纯逻辑（plugins/hashsum/core）用例。
//
// 摘要算法本身是 Qt 提供的，这里要钉住的是**我们的用法**：输入按 UTF-8 取字节、
// 输出统一小写十六进制、分块读文件的结果必须与一次性算的一致。
class TestHashSum : public QObject
{
    Q_OBJECT

private slots:
    void md5OfEmptyInputIsTheWellKnownEmptyDigest();
    void md5OfAbcMatchesTheRfcVector();
    void sha1OfAbcMatchesTheFipsVector();
    void sha256OfAbcMatchesTheFipsVector();
    void sha512OfAbcMatchesTheFipsVector();
    void everyAlgorithmGivesADifferentDigest();
    void digestIsLowercaseHex();
    void chineseTextIsHashedAsUtf8Bytes();
    void algorithmNamesAreStable();
    void allAlgorithmsCoversEveryEnumValue();

    void hashFileMatchesHashBytesAcrossChunkBoundaries();
    void hashFileReportsMissingPath();
    void hashFileReportsTooLargeWhenOverTheLimit();
};

void TestHashSum::md5OfEmptyInputIsTheWellKnownEmptyDigest()
{
    // 空输入的 MD5 是个著名常量 —— 顺手校验「没点计算时不会给出别的东西」。
    QCOMPARE(hashsum::hashBytes(QByteArray(), hashsum::Algorithm::Md5),
             QStringLiteral("d41d8cd98f00b204e9800998ecf8427e"));
}

void TestHashSum::md5OfAbcMatchesTheRfcVector()
{
    QCOMPARE(hashsum::hashBytes(QByteArrayLiteral("abc"), hashsum::Algorithm::Md5),
             QStringLiteral("900150983cd24fb0d6963f7d28e17f72"));
}

void TestHashSum::sha1OfAbcMatchesTheFipsVector()
{
    QCOMPARE(hashsum::hashBytes(QByteArrayLiteral("abc"), hashsum::Algorithm::Sha1),
             QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d"));
}

void TestHashSum::sha256OfAbcMatchesTheFipsVector()
{
    QCOMPARE(hashsum::hashBytes(QByteArrayLiteral("abc"), hashsum::Algorithm::Sha256),
             QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

void TestHashSum::sha512OfAbcMatchesTheFipsVector()
{
    QCOMPARE(hashsum::hashBytes(QByteArrayLiteral("abc"), hashsum::Algorithm::Sha512),
             QStringLiteral("ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
                            "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"));
}

void TestHashSum::everyAlgorithmGivesADifferentDigest()
{
    // 界面上一次列出四种算法；若哪天接错成同一个，用户会拿着四个相同的值发懵。
    const QByteArray data = QByteArrayLiteral("toolbox");
    QSet<QString> seen;
    for (const hashsum::Algorithm algo : hashsum::allAlgorithms()) {
        const QString digest = hashsum::hashBytes(data, algo);
        QVERIFY(!digest.isEmpty());
        seen.insert(digest);
    }
    QCOMPARE(seen.size(), hashsum::allAlgorithms().size());
}

void TestHashSum::digestIsLowercaseHex()
{
    // 校验和常要拿去和别处的值比对，大小写不统一会白折腾一轮。
    const QString digest =
        hashsum::hashBytes(QByteArrayLiteral("AbC-123"), hashsum::Algorithm::Sha256);
    QVERIFY(!digest.isEmpty());
    QCOMPARE(digest, digest.toLower());
    // 只该出现十六进制字符：多一个冒号或空格都会让「拿去别处比对」失败。
    QVERIFY(digest.contains(QRegularExpression(QStringLiteral("^[0-9a-f]+$"))));
}

void TestHashSum::chineseTextIsHashedAsUtf8Bytes()
{
    // 与 base64 同一条约定：QString 内部是 UTF-16，取字节必须显式 toUtf8()。
    // 写成按 UTF-16 字节算的话，同一个串会得出另一个摘要，且与别处算的值对不上 ——
    // 所以这里明确断言两种取法**必须不同**（自比较是假绿，测不出任何东西）。
    const QString text = QStringLiteral("中文");
    const QByteArray utf8 = text.toUtf8();
    QCOMPARE(utf8.size(), 6); // 两个汉字各 3 字节

    const QByteArray utf16(reinterpret_cast<const char *>(text.utf16()),
                           static_cast<int>(text.size()) * 2);
    const QString byUtf8 = hashsum::hashBytes(utf8, hashsum::Algorithm::Sha256);
    const QString byUtf16 = hashsum::hashBytes(utf16, hashsum::Algorithm::Sha256);
    QVERIFY(byUtf8 != byUtf16);
}

void TestHashSum::algorithmNamesAreStable()
{
    // 算法名既写进结果文本，也用作配置里勾选状态的键 —— 改名会让老用户的
    // 勾选记录悄悄失效，所以钉住。
    QCOMPARE(hashsum::algorithmName(hashsum::Algorithm::Md5), QStringLiteral("MD5"));
    QCOMPARE(hashsum::algorithmName(hashsum::Algorithm::Sha1), QStringLiteral("SHA-1"));
    QCOMPARE(hashsum::algorithmName(hashsum::Algorithm::Sha256), QStringLiteral("SHA-256"));
    QCOMPARE(hashsum::algorithmName(hashsum::Algorithm::Sha512), QStringLiteral("SHA-512"));
}

void TestHashSum::allAlgorithmsCoversEveryEnumValue()
{
    // 新增算法时若忘了加进列表，界面上就永远选不到它 —— 这条让遗漏变成红灯。
    const auto all = hashsum::allAlgorithms();
    QCOMPARE(all.size(), 4);
    QVERIFY(all.contains(hashsum::Algorithm::Md5));
    QVERIFY(all.contains(hashsum::Algorithm::Sha1));
    QVERIFY(all.contains(hashsum::Algorithm::Sha256));
    QVERIFY(all.contains(hashsum::Algorithm::Sha512));
}

void TestHashSum::hashFileMatchesHashBytesAcrossChunkBoundaries()
{
    // 1.5 MB 会跨过 1 MB 的分块边界：这条钉住「分块读出来的结果和一次性算的
    // 完全一致」—— 分块逻辑最容易错在最后一块的长度上。
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QByteArray payload(1500000, 'x');
    const QString path = dir.path() + QStringLiteral("/sample.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(payload), static_cast<qint64>(payload.size()));
    file.close();

    for (const hashsum::Algorithm algo : hashsum::allAlgorithms()) {
        const hashsum::HashResult fromFile = hashsum::hashFile(path, algo);
        QVERIFY(fromFile.ok);
        QCOMPARE(fromFile.text, hashsum::hashBytes(payload, algo));
    }
}

void TestHashSum::hashFileReportsMissingPath()
{
    const hashsum::HashResult result = hashsum::hashFile(
        QStringLiteral("C:/__toolbox_no_such_file__/x.bin"), hashsum::Algorithm::Sha256);
    QVERIFY(!result.ok);
    QVERIFY(result.error == hashsum::FileError::NotAFile);
    QVERIFY(result.text.isEmpty());
}

void TestHashSum::hashFileReportsTooLargeWhenOverTheLimit()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.path() + QStringLiteral("/small.bin");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QByteArray(100, 'y'));
    file.close();

    const hashsum::HashResult limited = hashsum::hashFile(path, hashsum::Algorithm::Md5, 10);
    QVERIFY(!limited.ok);
    QVERIFY(limited.error == hashsum::FileError::TooLarge);

    // 给足上限就应当成功 —— 否则说明上限判断把正常文件也挡了。
    const hashsum::HashResult ok = hashsum::hashFile(path, hashsum::Algorithm::Md5, 1024);
    QVERIFY(ok.ok);
}

QTEST_APPLESS_MAIN(TestHashSum)

#include "tst_hashsum.moc"
