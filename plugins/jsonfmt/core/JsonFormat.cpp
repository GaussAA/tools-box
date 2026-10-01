#include "JsonFormat.h"

#include <QJsonDocument>
#include <QJsonParseError>

namespace jsonfmt {

FormatResult formatJson(const QString &text, bool compact)
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
    result.text = QString::fromUtf8(
        document.toJson(compact ? QJsonDocument::Compact : QJsonDocument::Indented));
    return result;
}

} // namespace jsonfmt
