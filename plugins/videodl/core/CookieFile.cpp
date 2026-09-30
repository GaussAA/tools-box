#include "CookieFile.h"

#include <QDir>
#include <QFile>

namespace videodl {

QString normalizedCookiesPath()
{
    return QDir::tempPath() + QStringLiteral("/toolbox-cookies.txt");
}

QString normalizeCookies(const QString &source, const QString &target, int *fixedRows,
                         int *droppedRows)
{
    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QStringList lines = QString::fromUtf8(in.readAll()).split(QLatin1Char('\n'));
    in.close();

    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return QString();
    }

    QString text = QStringLiteral("# Netscape HTTP Cookie File\n");
    int fixed = 0;
    int dropped = 0;
    for (QString line : lines) {
        line.remove(QLatin1Char('\r'));
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }

        // 原文件的注释一概不要，头部标记由我们统一给；但 #HttpOnly_ 不是
        // 注释，它是 HttpOnly cookie 的标记，必须原样带回去。
        const bool httpOnly = trimmed.startsWith(QStringLiteral("#HttpOnly_"));
        if (trimmed.startsWith(QLatin1Char('#')) && !httpOnly) {
            continue;
        }

        const QString body = httpOnly ? trimmed.mid(10) : trimmed;
        QStringList fields = body.split(QLatin1Char('\t'));
        if (fields.size() < 7 || fields.at(5).trimmed().isEmpty()) {
            ++dropped;
            continue;
        }

        // 第 2 列必须与前导「.」自洽，否则 cookiejar 断言失败。
        const QString want = fields.at(0).startsWith(QLatin1Char('.'))
                                 ? QStringLiteral("TRUE")
                                 : QStringLiteral("FALSE");
        if (fields.at(1) != want) {
            fields[1] = want;
            ++fixed;
        }

        if (httpOnly) {
            text += QStringLiteral("#HttpOnly_");
        }
        text += fields.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }

    out.write(text.toUtf8());
    out.close();

    if (fixedRows) {
        *fixedRows = fixed;
    }
    if (droppedRows) {
        *droppedRows = dropped;
    }
    return target;
}

} // namespace videodl
