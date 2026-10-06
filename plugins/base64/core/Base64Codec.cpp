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

} // namespace base64
