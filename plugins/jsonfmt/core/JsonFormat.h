#pragma once

#include <QString>

// JSON 格式化插件里「解析与序列化」的那部分纯逻辑。
//
// 这段逻辑以前写在页面的 lambda 里，只能靠手工点按钮验证；抽到这里之后可以用
// 任意字符串直接单测（判定标准见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace jsonfmt {

/// 一次格式化/压缩的结果。
struct FormatResult
{
    bool ok = false;       ///< 输入是不是合法 JSON
    QString text;          ///< 成功时的输出文本
    int errorOffset = -1;  ///< 失败时的出错偏移（字节）；-1 表示没有位置信息
    QString errorText;     ///< 失败原因，**非界面文案**：Qt 给的错误描述，由 View 去 tr()
};

/// 把 text 按 compact 指定的样式重新序列化。
///
/// 输入不合法时 ok 为 false，errorOffset / errorText 带上出错位置与原因 ——
/// 只给「解析失败」四个字对定位问题毫无帮助，位置必须回传。
FormatResult formatJson(const QString &text, bool compact);

} // namespace jsonfmt
