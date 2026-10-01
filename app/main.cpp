#include "MainWindow.h"
#include "core/LanguageChoice.h"
#include "core/Logger.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>
#include <QStandardPaths>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QSettings 用这两个名字决定配置存放位置，必须在构造 QSettings 之前设置。
    QApplication::setOrganizationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationVersion(QStringLiteral(TOOLBOX_VERSION));

    // 结构化日志：把 qDebug/qInfo/qWarning/qCritical 重定向到磁盘文件 + 控制台。
    // 必须在任何业务日志之前安装；日志落在 AppData/ToolBox/toolbox.log。
    {
        const QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        const QString logPath = logDir + QStringLiteral("/toolbox.log");
        toolbox::Logger::install(logPath);
    }

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
    // **前缀必须是 `qtbase`，不能用 `qt`**：Qt 6 把实际词条移到了 qtbase_<locale>.qm，
    // 而 qt_<locale>.qm 退化成一个 99 字节的空壳（Qt 6.12 实测：qtbase_zh_CN.qm
    // 147 KB，qt_zh_CN.qm 99 字节）。按 `qt` 加载会「成功」装上一个什么都没翻译的
    // translator，症状是消息框按钮一直是 "OK" 而不是「确定」—— 而加载本身不报错，
    // 所以只能靠 verify_shell.ps1 那条「按钮是否被本地化」的断言逮住。
    // 保留 `qt` 作为回退：万一某个 Qt 版本只带老文件名，也不至于退回英文。
    //
    // 用 selection.locale 而不是 QLocale::system()：在中文系统上强制英文时，
    // 否则会出现「工具自己的文案是英文、Qt 的对话框按钮还是中文」。
    QTranslator qtTranslator;
    const QString deployedDir =
        QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    const QString qtTranslationsDir = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
    const bool translationsLoaded = qtTranslator.load(selection.locale, QStringLiteral("qtbase"),
                                                      QStringLiteral("_"), deployedDir)
        || qtTranslator.load(selection.locale, QStringLiteral("qtbase"), QStringLiteral("_"),
                             qtTranslationsDir)
        || qtTranslator.load(selection.locale, QStringLiteral("qt"), QStringLiteral("_"),
                             deployedDir)
        || qtTranslator.load(selection.locale, QStringLiteral("qt"), QStringLiteral("_"),
                             qtTranslationsDir);
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
