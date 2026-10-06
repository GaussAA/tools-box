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

/// 把 Base64 文本解回原文。
///
/// 输入首尾的空白与换行会被去掉（从聊天软件/记事本粘贴时常带），解码失败时
/// 返回空串——Qt 的解码器对非法字符是宽容的（会跳过），因此「解不出来」与
/// 「解出空串」无法区分，调用方若要区分需自行校验输入。
QString decodeBase64(const QString &text, bool urlSafe);

} // namespace base64
