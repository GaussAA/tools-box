#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // QSettings 用这两个名字决定配置存放位置，必须在构造 QSettings 之前设置。
    QApplication::setOrganizationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationName(QStringLiteral("ToolBox"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    MainWindow window;
    window.show();

    return app.exec();
}