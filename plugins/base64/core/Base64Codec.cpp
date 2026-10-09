#include "core/Base64Codec.h"

#include <QByteArray>

namespace base64 {

namespace {

/// 两种字符集的唯一差别，其余行为一致。
QByteArray::Base64Options optionsFor(bool urlSafe)
{
    return urlSafe ? QByteArray::Base64UrlEncoding : QByteArray::Base64Encoding;
}

} // namespace

QString encodeBase64(const QString &text, bool urlSafe)
{
    // 走 UTF-8：QString 内部是 UTF-16，直接 toUtf8 才是编码的正确字节序列。
    const QByteArray encoded = text.toUtf8().toBase64(optionsFor(urlSafe));
    return QString::fromLatin1(encoded);
}

QString decodeBase64(const QString &text, bool urlSafe)
{
    // trimmed()：粘贴来的文本常带首尾空白与换行，不去掉会被解码器当成非法字符跳过。
    const QByteArray decoded =
        QByteArray::fromBase64(text.trimmed().toLatin1(), optionsFor(urlSafe));
    return QString::fromUtf8(decoded);
}

DecodeResult decodeBase64Checked(const QString &text, bool urlSafe)
{
    DecodeResult result;

    // 与 decodeBase64() 保持一致地先去首尾空白，否则「粘贴带了换行」会被误判为非法字符。
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        result.error = DecodeError::Empty;
        return result;
    }

    // Base64 以 4 个字符为一组，长度不是 4 的倍数说明输入被截断或压根不是 Base64。
    if (trimmed.size() % 4 != 0) {
        result.error = DecodeError::BadLength;
        return result;
    }

    const QString alphabet = urlSafe
        ? QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
        : QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/");

    int padding = 0;
    for (int i = 0; i < trimmed.size(); ++i) {
        const QChar ch = trimmed.at(i);
        if (ch == QLatin1Char('=')) {
            // 补位最多两个，且只能出现在末尾 —— 早于末尾两位就说明是乱写的。
            if (i < trimmed.size() - 2) {
                result.error = DecodeError::BadCharacter;
                return result;
            }
            ++padding;
            continue;
        }
        // 补位之后不该再出现别的字符；字符必须在字符集内。
        if (padding > 0 || !alphabet.contains(ch)) {
            result.error = DecodeError::BadCharacter;
            return result;
        }
    }

    result.ok = true;
    result.text = decodeBase64(trimmed, urlSafe);
    return result;
}

} // namespace base64
