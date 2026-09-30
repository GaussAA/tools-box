#include "MainWindow.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QSettings 用这两个名字决定配置存放位置，必须在构造 QSettings 之前设置。
    QApplication::setOrganizationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationVersion(QStringLiteral(TOOLBOX_VERSION));

    // Qt 自带控件（消息框按钮、文件对话框、输入框右键菜单）的文案来自 Qt 的翻译
    // 文件，不装这个 QTranslator 就一直是英文。文件由 deploy 目标的 --translations
    // 放到 <exe 目录>/translations/；直接跑构建产物时该目录还不存在，回退到 Qt
    // 安装目录，两种情况都能用（见偏差 9.6）。
    QTranslator qtTranslator;
    const QString deployedDir = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    const bool translationsLoaded
        = qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"), deployedDir)
        || qtTranslator.load(QLocale(), QStringLiteral("qt"), QStringLiteral("_"),
                             QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    if (translationsLoaded) {
        QApplication::installTranslator(&qtTranslator);
    }

    MainWindow window;
    window.show();

    return app.exec();
}
