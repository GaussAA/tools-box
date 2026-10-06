#include "EngineLocator.h"

#include <QFileInfo>
#include <QStandardPaths>

namespace videodl {

QString engineDir(const QString &appDir)
{
    return appDir + QStringLiteral("/tools/bin");
}

QString resolveExecutable(const QString &manual, const QString &fileName, const QString &baseDir,
                          const QString &sharedDir)
{
    if (!manual.isEmpty() && QFileInfo::exists(manual)) {
        return QFileInfo(manual).absoluteFilePath();
    }

    const QString bundled = baseDir + QLatin1Char('/') + fileName;
    if (QFileInfo::exists(bundled)) {
        return bundled;
    }

    // PATH 里可能带扩展名也可能不带，两种写法都试一遍。
    const QString stem = QFileInfo(fileName).completeBaseName();
    for (const QString &name : {stem, fileName}) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty()) {
            return found;
        }
    }

    // 最后一级兜底：用户配置的共享目录（跨工具复用外部内核，见头文件注释）。
    if (!sharedDir.isEmpty()) {
        const QString shared = sharedDir + QLatin1Char('/') + fileName;
        if (QFileInfo::exists(shared)) {
            return QFileInfo(shared).absoluteFilePath();
        }
    }

    return QString();
}

} // namespace videodl
