#include "core/StegoPayload.h"

#include <QtGlobal>

#include <array>

namespace imgwatermark {
namespace {

/// 生成 CRC32 查找表（8×256，索引是字节，值是该字节的移位结果）。
///
/// 用一次性初始化的静态局部量而非每次重算：一张 4000×3000 的图要处理上百个
/// 冗余块，每个块都重算 256 项是纯浪费。函数内静态的初始化在 C++11 起是线程
/// 安全的，多线程（后台执行）调用也不会出问题。
const quint32 *crcTable()
{
    static const std::array<quint32, 256> table = []() {
        std::array<quint32, 256> values = {};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1u) != 0u ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            values[static_cast<std::size_t>(i)] = value;
        }
        return values;
    }();
    return table.data();
}

/// 大端追加一个 32 位值。
///
/// 载荷里所有多字节字段都用大端：字节序与平台无关，跨机器/跨架构解出来的
/// 结果一致（小端写法会在别的机器上解出乱码，且难以复现）。
void appendBigEndian32(QByteArray *out, quint32 value)
{
    out->append(static_cast<char>((value >> 24) & 0xFFu));
    out->append(static_cast<char>((value >> 16) & 0xFFu));
    out->append(static_cast<char>((value >> 8) & 0xFFu));
    out->append(static_cast<char>(value & 0xFFu));
}

/// 读出大端 32 位值。
quint32 readBigEndian32(const QByteArray &data, int offset)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData() + offset);
    return (static_cast<quint32>(bytes[0]) << 24) | (static_cast<quint32>(bytes[1]) << 16)
        | (static_cast<quint32>(bytes[2]) << 8) | static_cast<quint32>(bytes[3]);
}

} // namespace

quint32 crc32(const QByteArray &data, quint32 seed)
{
    const quint32 *table = crcTable();
    quint32 crc = seed;
    for (char rawByte : data) {
        // 用无符号扩展取低 8 位：char 在 MSVC 上是有符号的，直接强转会走
        // 实现定义的符号扩展，高位字节的 CRC 全部算错。
        const auto byte = static_cast<unsigned char>(rawByte);
        const quint32 index = (crc ^ byte) & 0xFFu;
        crc = table[index] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

QByteArray buildPayload(const QByteArray &payload)
{
    if (payload.isEmpty() || payload.size() > kMaxPayloadBytes) {
        return QByteArray();
    }

    QByteArray out;
    out.reserve(kPayloadHeaderBytes + payload.size());
    out.append(kPayloadMagicFirst);
    out.append(kPayloadMagicSecond);
    out.append(static_cast<char>(kPayloadVersion));
    const auto length = static_cast<quint16>(payload.size());
    out.append(static_cast<char>((length >> 8) & 0xFFu));
    out.append(static_cast<char>(length & 0xFFu));
    // CRC 只覆盖正文：头本身是固定的，换一个头格式不需要重算正文的校验。
    appendBigEndian32(&out, crc32(payload));
    out.append(payload);
    return out;
}

bool parsePayload(const QByteArray &raw, QByteArray *payloadOut, QString *errorOut)
{
    const auto fail = [errorOut](const QString &reason) {
        if (errorOut != nullptr) {
            *errorOut = reason;
        }
        return false;
    };

    if (raw.size() < kPayloadHeaderBytes) {
        return fail(QStringLiteral("数据长度不足一个水印头"));
    }

    if (raw.at(0) != kPayloadMagicFirst || raw.at(1) != kPayloadMagicSecond) {
        return fail(QStringLiteral("水印魔数不匹配"));
    }

    const auto version = static_cast<unsigned char>(raw.at(2));
    if (version != kPayloadVersion) {
        return fail(QStringLiteral("水印版本不支持：%1").arg(version));
    }

    const auto *lengthBytes = reinterpret_cast<const unsigned char *>(raw.constData() + 3);
    const auto length =
        static_cast<int>((static_cast<quint16>(lengthBytes[0]) << 8) | lengthBytes[1]);
    if (length <= 0 || length > kMaxPayloadBytes) {
        return fail(QStringLiteral("水印长度字段非法：%1").arg(length));
    }
    // 长度字段说多少字节，raw 就该有多少字节。多或少都说明比特流已经错位
    // ——继续按长度读只会读到垃圾。
    if (raw.size() != kPayloadHeaderBytes + length) {
        return fail(QStringLiteral("水印长度与实际数据不符"));
    }

    const QByteArray payload = raw.mid(kPayloadHeaderBytes);
    const quint32 expected = readBigEndian32(raw, 5);
    if (crc32(payload) != expected) {
        return fail(QStringLiteral("水印校验失败，数据已损坏"));
    }

    if (payloadOut != nullptr) {
        *payloadOut = payload;
    }
    if (errorOut != nullptr) {
        errorOut->clear();
    }
    return true;
}

} // namespace imgwatermark
