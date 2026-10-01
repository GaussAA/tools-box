#include "MainWindow.h"
#include "core/LanguageChoice.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QSettings 用这两个名字决定配置存放位置，必须在构造 QSettings 之前设置。
    QApplication::setOrganizationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationVersion(QStringLiteral(TOOLBOX_VERSION));

    // 界面语言：配置里的 ui/language 为空表示跟随系统（见 docs/workflow.md §7）。
    // 只在这里定一次，运行期不切换 —— 改语言要重启，同时这也是 ToolMeta::name
    // 能按「同一次装载期间稳定」缓存的前提（架构 §4.3）。
    const QString configuredLanguage = QSettings().value(QStringLiteral("ui/language")).toString();
    const toolbox::UiLanguageSelection selection =
        toolbox::resolveUiLanguage(configuredLanguage, QLocale::system());

    // Qt 自带控件（消息框按钮、文件对话框、输入框右键菜单）的文案来自 Qt 的翻译
    // 文件，不装这个 QTranslator 就一直是英文。文件由 deploy 目标的 --translations
    // 放到 <exe 目录>/translations/；直接跑构建产物时该目录还不存在，回退到 Qt
    // 安装目录，两种情况都能用（见偏差 9.6）。
    //
    // 用 selection.locale 而不是 QLocale::system()：在中文系统上强制英文时，
    // 否则会出现「工具自己的文案是英文、Qt 的对话框按钮还是中文」。
    QTranslator qtTranslator;
    const QString deployedDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    const bool translationsLoaded =
        qtTranslator.load(selection.locale, QStringLiteral("qt"), QStringLiteral("_"), deployedDir)
        || qtTranslator.load(selection.locale, QStringLiteral("qt"), QStringLiteral("_"),
                             QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    if (translationsLoaded) {
        QApplication::installTranslator(&qtTranslator);
    }

    // 工具箱自己的文案。中文是源语言，不需要翻译文件；英文来自编进可执行文件的
    // 资源（顶层 CMakeLists.txt 的 qt_add_translations 打的 /i18n/toolbox_en.qm）。
    QTranslator appTranslator;
    if (selection.language == toolbox::UiLanguage::English
        && appTranslator.load(QStringLiteral(":/i18n/toolbox_en.qm"))) {
        QApplication::installTranslator(&appTranslator);
    }

    MainWindow window;
    window.show();

    return app.exec();
}
