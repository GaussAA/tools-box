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
    // 配置里写的是 en_US / zh_CN 这类**带地区**的代码时就用它本身；只写了语言
    // （"zh"、"en"）时必须补上默认地区。
    //
    // 为什么不能直接把 "zh" 原样用：Qt 自带的翻译文件是按**地区**命名的，只有
    // qtbase_zh_CN.qm / qtbase_zh_TW.qm，没有 qtbase_zh.qm。拿 QLocale("zh")
    // （name() 恰好是 "zh"）去 load，只会一个文件都找不到，然后**静默**什么都不装 ——
    // 表现是消息框按钮一直是 "OK" 而不是「确定」，而工具自己的中文文案完全正常，
    // 于是很难联想到是这里。配置成 "zh" 是最自然的写法，所以这条路一定会被走到。
    const QLocale parsed(configured.trimmed());
    const bool regionSpecified = parsed.name().contains(QLatin1Char('_'));
    const QLocale::Language wanted =
        language == UiLanguage::English ? QLocale::English : QLocale::Chinese;
    if (parsed.language() == wanted && regionSpecified) {
        return parsed;
    }

    // 英文是 Qt 的源语言，本来就不需要翻译文件；给个地区只是为了保持一致。
    if (language == UiLanguage::English) {
        return QLocale(parsed.language() == QLocale::English ? parsed.name()
                                                             : QStringLiteral("en_US"));
    }
    return QLocale(QStringLiteral("zh_CN"));
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
