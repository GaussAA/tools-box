#pragma once

#include <QLocale>
#include <QString>

namespace toolbox {

/// 界面语言。中文是**源语言**（`tr()` 里写的就是中文，不需要翻译文件），
/// 英文走编进可执行文件的 `toolbox_en.qm`。
enum class UiLanguage { Chinese, English };

/// 界面语言的解析结果。
struct UiLanguageSelection
{
    UiLanguage language = UiLanguage::Chinese;
    /// 与 `language` 配套的地区。装 **Qt 自带**的翻译（消息框按钮、文件对话框）
    /// 要用它而不是 `QLocale::system()`：否则在中文系统上强制英文时，会出现
    /// 「工具自己的文案是英文、Qt 的对话框按钮还是中文」这种半吊子结果。
    QLocale locale;
};

/// 决定这次启动用哪个界面语言。
///
/// `configured` 是配置里的 `ui/language`：**空串表示跟随系统**；非空时按语言代码
/// 强制（只认 `en*` 与 `zh*`，大小写不敏感，所以 `en_US`/`en_GB` 都是英文、
/// `zh_CN`/`zh_TW` 都是中文）。认不出来的值**不作为错误**，而是退回跟随系统 ——
/// 配置写坏不该让程序起不来。
///
/// `systemLocale` 由调用方传 `QLocale::system()`，这样测试里可以喂任意地区。
///
/// 只做判断，不构造 `QSettings`、也不碰 `QTranslator`：这段逻辑因此可以脱离
/// `QApplication` 单测（判定标准见 docs/architecture.md §3，用例见
/// tests/tst_languagechoice.cpp）。
///
/// 语言**只在启动时定一次**，运行期不切换。这既是取舍（改语言要重启），也是
/// `ToolMeta::name` 能按「同一次装载期间稳定」缓存的前提 —— 见架构 §4.3。
UiLanguageSelection resolveUiLanguage(const QString &configured, const QLocale &systemLocale);

} // namespace toolbox