#include "HashSum.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace hashsum {

namespace {

/// 一次读多少。1 MB 是个折中：太小则读大文件时系统调用次数多，太大则临时
/// 缓冲占用高。
constexpr qint64 kChunkBytes = 1024 * 1024;

QCryptographicHash::Algorithm qtAlgorithm(Algorithm algo)
{
    switch (algo) {
    case Algorithm::Md5:
        return QCryptographicHash::Md5;
    case Algorithm::Sha1:
        return QCryptographicHash::Sha1;
    case Algorithm::Sha256:
        return QCryptographicHash::Sha256;
    case Algorithm::Sha512:
        return QCryptographicHash::Sha512;
    }
    return QCryptographicHash::Md5;
}

} // namespace

QString algorithmName(Algorithm algo)
{
    switch (algo) {
    case Algorithm::Md5:
        return QStringLiteral("MD5");
    case Algorithm::Sha1:
        return QStringLiteral("SHA-1");
    case Algorithm::Sha256:
        return QStringLiteral("SHA-256");
    case Algorithm::Sha512:
        return QStringLiteral("SHA-512");
    }
    return {};
}

QList<Algorithm> allAlgorithms()
{
    return {Algorithm::Md5, Algorithm::Sha1, Algorithm::Sha256, Algorithm::Sha512};
}

QString hashBytes(const QByteArray &data, Algorithm algo)
{
    QCryptographicHash hash(qtAlgorithm(algo));
    hash.addData(data);
    // toHex() 本身就是小写；用 fromLatin1 是因为十六进制串里不可能有非 ASCII。
    return QString::fromLatin1(hash.result().toHex());
}

HashResult hashFile(const QString &path, Algorithm algo, qint64 maxBytes)
{
    HashResult result;

    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = FileError::NotAFile;
        return result;
    }
    if (maxBytes > 0 && info.size() > maxBytes) {
        result.error = FileError::TooLarge;
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = FileError::CannotOpen;
        return result;
    }

    QCryptographicHash hash(qtAlgorithm(algo));
    QByteArray buffer;
    buffer.resize(static_cast<int>(kChunkBytes));
    while (!file.atEnd()) {
        const qint64 read = file.read(buffer.data(), kChunkBytes);
        if (read <= 0) {
            break;
        }
        // QByteArrayView 而非 (const char*, int)：后者在 Qt 6 已弃用，而本项目
        // 警告即错误（/WX），会直接编译不过。
        hash.addData(QByteArrayView(buffer.constData(), static_cast<qsizetype>(read)));
    }

    result.ok = true;
    result.text = QString::fromLatin1(hash.result().toHex());
    return result;
}

} // namespace hashsum
