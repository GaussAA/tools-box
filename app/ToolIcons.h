#pragma once

#include <QIcon>
#include <QString>

namespace toolbox {

/// 插件没提供图标时，用工具名首字符画一个圆角占位图标。
///
/// 色相由名字哈希决定 —— 保证同一个工具每次启动都是同一个颜色，否则每次
/// 重载插件，导航里的占位图标都会换一批颜色，看着像程序出了毛病。
QIcon fallbackToolIcon(const QString &name);

} // namespace toolbox
