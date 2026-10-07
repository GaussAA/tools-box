#pragma once

#include <QByteArray>
#include <QString>

// 数字水印插件的「载荷编解码」纯逻辑。
//
// 载荷是真正被嵌进像素的那串字节。外面套一层极简的头，是为了解决三个问题：
//
//   1. **不能误读**：解码时若不校验，图像本身的内容可能被当成水印（任何一段
//      8×8 块的中频系数都能凑出「像样的比特流」）。用 2 字节魔数 + 1 字节版本
//      做闸门，读出来先验明正身。
//   2. **不能静默出错**：有损压缩（JPEG 量化）必然改动一部分系数，被改的比特位
//      会让后续比特全部错位 —— 没有校验就只能吐出一串乱码，用户完全无从判断
//      「这是我的水印」还是「这是噪声」。CRC32 让「解失败了」成为一件可判定的事，
//      上层据此改用其它冗余块重试，而不是输出垃圾。
//   3. **便于演进**：版本号留给将来的算法升级。解出 v2 就走新算法，不必改判定逻辑。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace imgwatermark {

/// 魔数首字节（'W'）。与 kPayloadMagicSecond 合成 2 字节标识 "WA"。
inline constexpr char kPayloadMagicFirst = 'W';
/// 魔数次字节（'A'）。
inline constexpr char kPayloadMagicSecond = 'A';

/// 载荷格式版本号。改动载荷结构时递增，解码侧按版本分派。
inline constexpr unsigned char kPayloadVersion = 1;

/// 头部长度：魔数(2) + 版本(1) + 长度(2) + CRC32(4)。
inline constexpr int kPayloadHeaderBytes = 9;

/// 载荷正文的最大字节数。受长度字段（2 字节）限制。
inline constexpr int kMaxPayloadBytes = 0xFFFF;

/// CRC32（IEEE 802.3 多项式 0xEDB88320，初值与终值均取 0xFFFFFFFF）。
///
/// QtCore 没有公开的 CRC32 实现（QChecksum 是另一种更弱的算法），故自备。
/// 参数 seed 用于把多段数据连着算：本文件只用它算单段，但保留该参数以便复用。
quint32 crc32(const QByteArray &data, quint32 seed = 0xFFFFFFFFu);

/// 把一段原始字节封装成完整载荷（头 + 正文）。
///
/// @param payload 水印正文，长度须在 [1, kMaxPayloadBytes] 内。
/// @return 封装后的字节序列；正文为空或超长时返回空 QByteArray（调用方据此
///         判定失败，不要当成「合法的空载荷」继续嵌入）。
QByteArray buildPayload(const QByteArray &payload);

/// 解析载荷：验魔数、验版本、验长度、验 CRC32，全部通过才交出正文。
///
/// 任何一个环节不通过都返回 false —— 这是有意的**严格**校验：宁可报告「没找到
/// 有效水印」，也不能把损坏的比特流当成水印交出去。
///
/// @param raw 从图像里解出的原始字节（不含任何外部信息）。
/// @param payloadOut 成功时写入正文；失败时**不被修改**（调用方可以复用同一变量）。
/// @param errorOut   失败时写入原因（供上层给出明确提示）。
bool parsePayload(const QByteArray &raw, QByteArray *payloadOut, QString *errorOut);

} // namespace imgwatermark
