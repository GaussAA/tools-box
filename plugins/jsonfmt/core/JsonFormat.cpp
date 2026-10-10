#include "JsonFormat.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

namespace jsonfmt {

namespace {

/// Qt 的 Indented 输出固定用 4 个空格缩进，且每级必然是 4 的倍数。
/// 这里按行首空格数换算成目标单元 —— 只动行首，不动行内；JSON 的字符串里
/// 不会有真实换行（换行被转义成 \n），所以行首空格只可能是缩进。
QString reindent(const QString &text, const QString &unit)
{
    if (unit == QStringLiteral("    ")) {
        return text; // 基准就是 4 空格，不必逐行折腾
    }

    const QChar newline = QLatin1Char('\n');
    QStringList lines = text.split(newline);
    for (QString &line : lines) {
        int spaces = 0;
        while (spaces < line.size() && line.at(spaces) == QLatin1Char(' ')) {
            ++spaces;
        }
        if (spaces == 0) {
            continue;
        }
        line.remove(0, spaces);
        line.prepend(unit.repeated(spaces / 4));
    }
    return lines.join(newline);
}

} // namespace

QString indentUnit(Indent indent)
{
    switch (indent) {
    case Indent::TwoSpaces:
        return QStringLiteral("  ");
    case Indent::FourSpaces:
        return QStringLiteral("    ");
    case Indent::Tab:
        return QStringLiteral("\t");
    }
    return QStringLiteral("    ");
}

FormatResult formatJson(const QString &text, bool compact, Indent indent)
{
    FormatResult result;

    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError) {
        result.errorOffset = static_cast<int>(error.offset);
        result.errorText = error.errorString();
        return result;
    }

    result.ok = true;
    const QByteArray raw =
        document.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented);
    result.text =
        compact ? QString::fromUtf8(raw) : reindent(QString::fromUtf8(raw), indentUnit(indent));
    return result;
}

ErrorLocation locateError(const QString &text, int offset)
{
    ErrorLocation location;
    if (offset < 0) {
        return location;
    }

    // 偏移是字节偏移：先按 UTF-8 截断再数行数，含中文的 JSON 才不会指错列
    // （一个汉字占 3 字节，直接拿 QString 下标数会差出一倍多）。
    const QString prefix = QString::fromUtf8(text.toUtf8().left(offset));

    int line = 1;
    int lastBreak = -1;
    for (int i = 0; i < prefix.size(); ++i) {
        if (prefix.at(i) == QLatin1Char('\n')) {
            ++line;
            lastBreak = i;
        }
    }

    location.line = line;
    location.column = prefix.size() - lastBreak;
    return location;
}

} // namespace jsonfmt
