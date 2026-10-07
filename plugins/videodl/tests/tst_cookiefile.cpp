#include "core/CookieFile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

// cookies.txt 的规范化规则。
//
// 产物写到显式传入的 target，所以测试可以用 QTemporaryDir 接住，既不留残留，
// 也不需要去猜临时文件名（见 core/CookieFile.h）。
class TestCookieFile : public QObject
{
    Q_OBJECT

private slots:
    void normalizeFixesSubdomainFlagAndDropsMalformedRows();
    void normalizeReportsZeroCountsWhenSourceMissing();
    void normalizeFailsWhenTargetIsUnwritable();
    void normalizedCookiesPathIsStableAndUnderTempDir();
};

namespace {

QString writeSource(const QString &dir, const QByteArray &content)
{
    const QString path = dir + QStringLiteral("/cookies.txt");
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }
    file.write(content);
    file.close();
    return path;
}

QStringList readLines(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
}

} // namespace

void TestCookieFile::normalizeFixesSubdomainFlagAndDropsMalformedRows()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QByteArray source = "# Netscape HTTP Cookie File\n"
                              "# 原文件的注释应该被丢掉\n"
                              ".bilibili.com\tFALSE\t/\tFALSE\t0\tSESSDATA\tabc\n"
                              "#HttpOnly_.bilibili.com\tFALSE\t/\tFALSE\t0\tbili_jct\tdef\n"
                              "bogus-line\n"
                              "www.example.com\tTRUE\t/\tFALSE\t0\tname\tvalue\n"
                              "\n";
    const QString sourcePath = writeSource(temp.path(), source);
    QVERIFY(!sourcePath.isEmpty());

    const QString targetPath = temp.path() + QStringLiteral("/normalized.txt");
    int fixed = -1;
    int dropped = -1;

    QCOMPARE(videodl::normalizeCookies(sourcePath, targetPath, &fixed, &dropped), targetPath);
    // 两行的 includeSubDomains 列被补齐，一行的 TRUE 被改回 FALSE。
    QCOMPARE(fixed, 3);
    // bogus-line 字段数不足 7，整行丢掉。
    QCOMPARE(dropped, 1);

    const QStringList lines = readLines(targetPath);
    QCOMPARE(lines.first(), QStringLiteral("# Netscape HTTP Cookie File"));
    QVERIFY(lines.contains(
        QStringLiteral("#HttpOnly_.bilibili.com\tTRUE\t/\tFALSE\t0\tbili_jct\tdef")));
    QVERIFY(lines.contains(QStringLiteral("www.example.com\tFALSE\t/\tFALSE\t0\tname\tvalue")));

    const QString joined = lines.join(QLatin1Char('\n'));
    QVERIFY(!joined.contains(QStringLiteral("原文件的注释")));
    QVERIFY(!joined.contains(QStringLiteral("bogus-line")));
}

void TestCookieFile::normalizeReportsZeroCountsWhenSourceMissing()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    int fixed = -1;
    int dropped = -1;

    QVERIFY(videodl::normalizeCookies(temp.path() + QStringLiteral("/nope.txt"),
                                      temp.path() + QStringLiteral("/out.txt"), &fixed, &dropped)
                .isEmpty());
    // 出参必须是确定值，调用方不必自己预先清零。
    QCOMPARE(fixed, 0);
    QCOMPARE(dropped, 0);
}

void TestCookieFile::normalizeFailsWhenTargetIsUnwritable()
{
    QTemporaryDir temp;
    QVERIFY(temp.isValid());

    const QString sourcePath = writeSource(temp.path(), "a\tb\n");
    QVERIFY(!sourcePath.isEmpty());

    int fixed = -1;
    int dropped = -1;

    // 目标目录不存在，写不进去；此时不能报成功，否则调用方会拿着一个假路径
    // 去喂 yt-dlp。
    QVERIFY(videodl::normalizeCookies(
                sourcePath, temp.path() + QStringLiteral("/missing-dir/out.txt"), &fixed, &dropped)
                .isEmpty());
}

void TestCookieFile::normalizedCookiesPathIsStableAndUnderTempDir()
{
    const QString path = videodl::normalizedCookiesPath();

    // 页面在「删除 cookie 文件」时用这个路径定位文件，生产代码里调了三处
    // （VideoDlPlugin.cpp）。所以它必须稳定：两次调用同一个进程内要一致，
    // 否则清理会删不掉上一次留下的文件。
    QCOMPARE(videodl::normalizedCookiesPath(), path);

    // 落在临时目录而不是用户主目录：cookie 文件是运行期产物，不该出现在
    // 用户会长期保留的地方，也不该写进安装目录（那常常是只读的）。
    QVERIFY(path.startsWith(QDir::tempPath()));
    QVERIFY(!path.isEmpty());

    // 文件名固定：名字散落在多处，进程重启后仍要指向同一个文件。
    QVERIFY(path.endsWith(QStringLiteral("/toolbox-cookies.txt")));
}

QTEST_APPLESS_MAIN(TestCookieFile)

#include "tst_cookiefile.moc"
