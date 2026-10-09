#pragma once

#include <QString>

// Base64 插件里「编解码」的那部分纯逻辑。
//
// 这段逻辑原先是页面里两个按钮的 lambda（见 Base64Plugin.cpp）：规则只有一行
// QByteArray::toBase64，却因为与界面混在一起而完全无法单测 —— 而它恰好是
// 唯一有正确性要求的部分：URL 安全字符集、含中文时的 UTF-8 往返、输入前后
// 空白与换行的处理，任何一条错了都只会表现为「结果看起来不对」。
// 抽到这里后与 jsonfmt 对称（判定标准见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace base64 {

/// 把原文编成 Base64 文本。
///
/// 内部按 UTF-8 取字节，所以中文能被正确编码（非 ASCII 文本必须如此）。
/// urlSafe 为真时用 URL 安全字符集（`-` `_` 代替 `+` `/`）。注意 Qt 的
/// Base64UrlEncoding **只换字符集、结尾补位 `=` 仍然保留**（要去掉补位得另外
/// 叠加 OmitPadding）——这里与页面原有的行为保持一致，不顺手改语义。
QString encodeBase64(const QString &text, bool urlSafe);

/// 把 Base64 文本解回原文（宽容）。
///
/// 输入首尾的空白与换行会被去掉（从聊天软件/记事本粘贴时常带）。Qt 的解码器对
/// 非法字符是**宽容**的：它把不认识的字符直接跳过，剩下的照样解。所以脏输入得到
/// 的不是空串，而是一堆替换字符 —— 「解不出来」与「解出空串」无法区分。
/// 界面要给出确定的反馈，请用下面的 decodeBase64Checked()。
QString decodeBase64(const QString &text, bool urlSafe);

/// 解码失败的原因。
enum class DecodeError {
    None,         ///< 输入合法
    Empty,        ///< 去掉首尾空白后是空串
    BadLength,    ///< 长度不是 4 的倍数（Base64 以 4 个字符为一组）
    BadCharacter, ///< 含字符集之外的字符，或补位 `=` 出现在不该出现的位置
};

/// 解码结果。ok 为假时 text 无意义，应看 error 决定给用户看哪句话。
struct DecodeResult
{
    bool ok = false;
    QString text;
    DecodeError error = DecodeError::None;
};

/// 先校验、再解码，把失败原因明确交回调用方。
///
/// 存在理由：宽容解码把「输入根本不是 Base64」伪装成「解出一串乱码」，界面上表现为
/// 静默失败 —— 与本项目「禁止静默失败、失败要说清原因」的规则冲突。校验规则刻意
/// 收紧到 Base64 的字面定义（4 字符一组、字符集受限、补位只在末尾），宁可把边缘输入
/// 判为不合法，也不让用户在错误结果上白忙一场。
DecodeResult decodeBase64Checked(const QString &text, bool urlSafe);

} // namespace base64
