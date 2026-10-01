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

QString sanitizeFileName(const QString &title)
{
    // 上限按 UTF-16 码元算：文件系统限制的是字符数，不是字节数。
    constexpr int kMaxLength = 120;

    QString name;
    name.reserve(title.size());
    for (const QChar ch : title) {
        // 控制字符不可见也不可控；/ \ : * ? " < > | 在 Windows 上要么非法、
        // 要么会被当成路径分隔符。一律换成下划线，而不是删掉 —— 换成下划线
        // 能保住标题的可读性，删掉会让相邻词粘在一起。
        const bool illegal = ch.unicode() < 0x20 || ch.unicode() == 0x7f
            || QStringLiteral("/\\:*?\"<>|").contains(ch);
        name.append(illegal ? QLatin1Char('_') : ch);
    }

    // 首尾的空白与点：以点开头/结尾的名字在 Windows 上要么非法、要么被隐含掉，
    // 而纯点（"."、".."）更是直接指向目录本身。
    while (!name.isEmpty() && (name.front().isSpace() || name.front() == QLatin1Char('.'))) {
        name.remove(0, 1);
    }
    while (!name.isEmpty() && (name.back().isSpace() || name.back() == QLatin1Char('.'))) {
        name.chop(1);
    }

    if (name.size() > kMaxLength) {
        name.truncate(kMaxLength);
        while (!name.isEmpty() && (name.back().isSpace() || name.back() == QLatin1Char('.'))) {
            name.chop(1);
        }
    }

    // 设备名（CON / PRN / AUX / NUL / COM1…）在任何目录下都指向设备而不是文件，
    // 撞上了加个前缀破开 —— 概率极低，代价为零。
    static const QRegularExpression reservedRe(
        QStringLiteral(R"(^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?$)"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedRe.match(name).hasMatch()) {
        name.prepend(QLatin1Char('_'));
    }

    return name;
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
