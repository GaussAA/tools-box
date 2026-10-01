// resolveUiLanguage 的用例。被测逻辑在 app/core/LanguageChoice.*，不依赖界面。
//
// 覆盖对象：配置为空时跟随系统；配置能强制语言且大小写不敏感；配置写坏时退回
// 跟随系统；非中文地区一律拿英文（本工程只有中英两种语言）；返回的 QLocale 与
// 语言一致（否则 Qt 自带翻译会和界面语言对不上）。

#include "core/LanguageChoice.h"

#include <QLocale>
#include <QtTest>

using toolbox::resolveUiLanguage;
using toolbox::UiLanguage;
using toolbox::UiLanguageSelection;

class TestLanguageChoice : public QObject
{
    Q_OBJECT

private slots:
    void followsSystemWhenNothingConfigured();
    void nonChineseSystemFallsBackToEnglish();
    void configuredValueForcesLanguage();
    void languageCodeMatchIsCaseInsensitiveAndIgnoresRegion();
    void unparsableConfiguredValueFallsBackToSystem();
    void blankConfiguredValueIsTreatedAsUnset();
    void localeAlwaysAgreesWithLanguage();
    void bareLanguageCodeGetsADefaultRegion();
};

void TestLanguageChoice::followsSystemWhenNothingConfigured()
{
    QCOMPARE(resolveUiLanguage(QString(), QLocale(QStringLiteral("zh_CN"))).language,
             UiLanguage::Chinese);
    // 繁体同样是中文地区。
    QCOMPARE(resolveUiLanguage(QString(), QLocale(QStringLiteral("zh_TW"))).language,
             UiLanguage::Chinese);
}

void TestLanguageChoice::nonChineseSystemFallsBackToEnglish()
{
    const QStringList regions{QStringLiteral("en_US"), QStringLiteral("en_GB"),
                              QStringLiteral("fr_FR"), QStringLiteral("ja_JP")};
    for (const QString &region : regions) {
        QCOMPARE(resolveUiLanguage(QString(), QLocale(region)).language, UiLanguage::English);
    }
}

void TestLanguageChoice::configuredValueForcesLanguage()
{
    // 中文系统上强制英文，以及反过来。
    QCOMPARE(resolveUiLanguage(QStringLiteral("en"), QLocale(QStringLiteral("zh_CN"))).language,
             UiLanguage::English);
    QCOMPARE(resolveUiLanguage(QStringLiteral("zh_CN"), QLocale(QStringLiteral("en_US"))).language,
             UiLanguage::Chinese);
}

void TestLanguageChoice::languageCodeMatchIsCaseInsensitiveAndIgnoresRegion()
{
    QCOMPARE(resolveUiLanguage(QStringLiteral("EN"), QLocale(QStringLiteral("zh_CN"))).language,
             UiLanguage::English);
    QCOMPARE(resolveUiLanguage(QStringLiteral("en_GB"), QLocale(QStringLiteral("zh_CN"))).language,
             UiLanguage::English);
    QCOMPARE(resolveUiLanguage(QStringLiteral("zh"), QLocale(QStringLiteral("en_US"))).language,
             UiLanguage::Chinese);
}

void TestLanguageChoice::unparsableConfiguredValueFallsBackToSystem()
{
    // 配置写坏不该让程序起不来，也不该静默变成某个固定语言。
    QCOMPARE(
        resolveUiLanguage(QStringLiteral("klingon"), QLocale(QStringLiteral("zh_CN"))).language,
        UiLanguage::Chinese);
    QCOMPARE(
        resolveUiLanguage(QStringLiteral("klingon"), QLocale(QStringLiteral("en_US"))).language,
        UiLanguage::English);
}

void TestLanguageChoice::blankConfiguredValueIsTreatedAsUnset()
{
    // 只有空白也算没配。
    QCOMPARE(resolveUiLanguage(QStringLiteral("   "), QLocale(QStringLiteral("zh_CN"))).language,
             UiLanguage::Chinese);
}

void TestLanguageChoice::localeAlwaysAgreesWithLanguage()
{
    // 跟随系统时原样带出系统地区（这样 zh_TW 还能用到 qt_zh_TW.qm）；
    // 强制时必须是该语言对应的地区，否则 Qt 自带的翻译会和界面语言对不上。
    const UiLanguageSelection followed =
        resolveUiLanguage(QString(), QLocale(QStringLiteral("zh_TW")));
    QCOMPARE(followed.locale.name(), QLocale(QStringLiteral("zh_TW")).name());

    const UiLanguageSelection forcedEn =
        resolveUiLanguage(QStringLiteral("en"), QLocale(QStringLiteral("zh_CN")));
    QCOMPARE(forcedEn.locale.language(), QLocale::English);

    const UiLanguageSelection forcedZh =
        resolveUiLanguage(QStringLiteral("zh_CN"), QLocale(QStringLiteral("en_US")));
    QCOMPARE(forcedZh.locale.language(), QLocale::Chinese);

    // 强制某语言时给了具体地区，就用那个地区。
    const UiLanguageSelection forcedRegion =
        resolveUiLanguage(QStringLiteral("en_GB"), QLocale(QStringLiteral("zh_CN")));
    QCOMPARE(forcedRegion.locale.name(), QLocale(QStringLiteral("en_GB")).name());
}

void TestLanguageChoice::bareLanguageCodeGetsADefaultRegion()
{
    // 只写语言代码（"zh"）是最自然的配置写法，但 QLocale("zh").name() 就是 "zh"，
    // 而 Qt 自带的翻译文件**只按地区命名**：有 qtbase_zh_CN.qm / qtbase_zh_TW.qm，
    // 没有 qtbase_zh.qm。原样传下去会一个都找不到，然后静默什么都不装 ——
    // 症状是消息框按钮一直是 "OK"（工具自己的中文文案却完全正常），很难联想到语言。
    // 所以这里必须补上默认地区。
    const UiLanguageSelection forcedZh =
        resolveUiLanguage(QStringLiteral("zh"), QLocale(QStringLiteral("en_US")));

    QCOMPARE(forcedZh.language, UiLanguage::Chinese);
    QVERIFY(forcedZh.locale.name().contains(QLatin1Char('_'))); // 必须带地区
    QCOMPARE(forcedZh.locale.name(), QLocale(QStringLiteral("zh_CN")).name());

    // 大小写不敏感，结果必须一样。
    QCOMPARE(
        resolveUiLanguage(QStringLiteral("ZH"), QLocale(QStringLiteral("en_US"))).locale.name(),
        QLocale(QStringLiteral("zh_CN")).name());
}

QTEST_APPLESS_MAIN(TestLanguageChoice)

#include "tst_languagechoice.moc"
