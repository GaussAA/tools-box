#include "OutputParsing.h"

#include <QChar>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace videodl {

QString decodeOutput(const QByteArray &bytes)
{
    const QString utf8 = QString::fromUtf8(bytes);
    if (!utf8.contains(QChar::ReplacementCharacter)) {
        return utf8;
    }
    return QString::fromLocal8Bit(bytes);
}

QString stripAnsi(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\x1b\\[[0-9;]*[A-Za-z]"));
    return QString(text).remove(re);
}

QString extractUrl(const QString &input)
{
    static const QRegularExpression urlRe(
        QStringLiteral(R"(https?://[A-Za-z0-9\-._~:/?#\[\]@!$&'()*+,;=%]+)"));
    const QRegularExpressionMatch match = urlRe.match(input);
    return match.hasMatch() ? match.captured(0) : input;
}

ProgressInfo parseProgress(const QString &line)
{
    static const QRegularExpression progressRe(
        QStringLiteral(R"(\[download\]\s+(\d+(?:\.\d+)?)%)"));
    static const QRegularExpression speedRe(QStringLiteral(R"(\bat\s+(\S+/s))"));
    static const QRegularExpression etaRe(QStringLiteral(R"(\bETA\s+(\S+))"));

    ProgressInfo info;
    const QRegularExpressionMatch progressMatch = progressRe.match(line);
    if (!progressMatch.hasMatch()) {
        return info;
    }

    info.matched = true;
    info.percent = static_cast<int>(progressMatch.captured(1).toDouble());

    const QRegularExpressionMatch speedMatch = speedRe.match(line);
    if (speedMatch.hasMatch()) {
        info.speed = speedMatch.captured(1);
    }
    const QRegularExpressionMatch etaMatch = etaRe.match(line);
    if (etaMatch.hasMatch()) {
        info.eta = etaMatch.captured(1);
    }
    return info;
}

OutputStage classifyStage(const QString &line)
{
    if (line.contains(QStringLiteral("[download] Destination:"))) {
        return OutputStage::DownloadStarting;
    }
    if (line.contains(QStringLiteral("[Merger]"))) {
        return OutputStage::Merging;
    }
    if (line.contains(QStringLiteral("[ExtractAudio]"))
        || line.contains(QStringLiteral("[ffmpeg]"))) {
        return OutputStage::ExtractingAudio;
    }
    return OutputStage::None;
}

QString parseDestination(const QString &line)
{
    static const QRegularExpression destRe(QStringLiteral(
        R"RE(^(?:\[download\]|\[ExtractAudio\])\s+Destination:\s+(.+)$|^\[Merger\]\s+Merging formats into\s+"(.+)"$)RE"));
    const QRegularExpressionMatch match = destRe.match(line);
    if (!match.hasMatch()) {
        return QString();
    }
    return match.captured(1).isEmpty() ? match.captured(2) : match.captured(1);
}

} // namespace videodl
