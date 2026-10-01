#include "core/LanguageChoice.h"

namespace toolbox {

namespace {

/// 把语言代码折算成支持的语言。
bool matchSupportedLanguage(const QString &code, UiLanguage *out)
{
    const QString normalized = code.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("en"))) {
        *out = UiLanguage::English;
        return true;
    }
    if (normalized.startsWith(QStringLiteral("zh"))) {
        *out = UiLanguage::Chinese;
        return true;
    }
    return false;
}

/// 强制某种语言时，用它对应的地区去装 Qt 自带的翻译。
QLocale forcedLocale(UiLanguage language, const QString &configured)
{
    // 配置里写的是 en_US 这类地区代码时就用它本身，否则退回该语言的默认地区。
    const QLocale parsed(configured.trimmed());
    if (parsed.language()
        == (language == UiLanguage::English ? QLocale::English : QLocale::Chinese)) {
        return parsed;
    }
    return QLocale(language == UiLanguage::English ? QStringLiteral("en")
                                                   : QStringLiteral("zh_CN"));
}

} // namespace

UiLanguageSelection resolveUiLanguage(const QString &configured, const QLocale &systemLocale)
{
    UiLanguage forced = UiLanguage::Chinese;
    // 认不出来的值不当错误处理，直接落到「跟随系统」：用户把配置写坏了，
    // 该看到的是程序照常起来，而不是一个启动失败。
    if (!configured.trimmed().isEmpty() && matchSupportedLanguage(configured, &forced)) {
        return UiLanguageSelection{forced, forcedLocale(forced, configured)};
    }

    // 跟随系统。中文地区用中文；其余一律英文 —— 本工程只有这两种语言，
    // 法语、日语等地区拿英文是能给出的最好结果。
    const UiLanguage language =
        systemLocale.language() == QLocale::Chinese ? UiLanguage::Chinese : UiLanguage::English;
    return UiLanguageSelection{language, systemLocale};
}

} // namespace toolbox