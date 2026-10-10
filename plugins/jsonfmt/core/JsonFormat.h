#pragma once

#include <QString>

// JSON 格式化插件里「解析与序列化」的那部分纯逻辑。
//
// 这段逻辑以前写在页面的 lambda 里，只能靠手工点按钮验证；抽到这里之后可以用
// 任意字符串直接单测（判定标准见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace jsonfmt {

/// 缩进样式。
///
/// 存在理由：Qt 的 QJsonDocument 只有一种缩进（Indented 固定 4 个空格），没有开关，
/// 而 2 空格与 Tab 恰恰是 JSON 最常见的两种偏好 —— 想要就得自己换算，见
/// JsonFormat.cpp 里的 reindent()。枚举值即界面下拉框的序号，存配置也存它。
enum class Indent {
    TwoSpaces,
    FourSpaces,
    Tab,
};

/// 缩进样式的缩进单元。core 只给数据，显示名由界面 tr()。
QString indentUnit(Indent indent);

/// 出错位置：第几行第几列，都从 1 开始。拿不到时两个字段都是 -1。
struct ErrorLocation
{
    int line = -1;
    int column = -1;
};

/// 一次格式化/压缩的结果。
struct FormatResult
{
    bool ok = false;       ///< 输入是不是合法 JSON
    QString text;          ///< 成功时的输出文本
    int errorOffset = -1;  ///< 失败时的出错偏移（字节）；-1 表示没有位置信息
    QString errorText;     ///< 失败原因，**非界面文案**：Qt 给的错误描述，由 View 去 tr()
};

/// 把 text 按 compact 指定的样式重新序列化；indent 只在非压缩时起作用。
///
/// 输入不合法时 ok 为 false，errorOffset / errorText 带上出错位置与原因 ——
/// 只给「解析失败」四个字对定位问题毫无帮助，位置必须回传。
FormatResult formatJson(const QString &text, bool compact, Indent indent = Indent::FourSpaces);

/// 把字节偏移换算成「第几行第几列」。
///
/// 偏移是**字节**偏移（QJsonParseError 给的），而用户要数的是行列，所以这里先按
/// UTF-8 截到该偏移再数换行 —— 否则含中文的 JSON 会指错位置（一个汉字 3 字节）。
ErrorLocation locateError(const QString &text, int offset);

} // namespace jsonfmt
