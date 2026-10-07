#pragma once

#include "ToolBoxPlugin.h"

#include <QObject>

/// 图片数字水印插件：为图片嵌入肉眼不可见的数字水印。
///
/// 与可见文本水印的根本区别：水印数据替换的是像素信息里的不可见部分
/// （本实现为 DCT 中频系数），肉眼看不到、也看不出图被改过，但能用同一个工具
/// 把原文取回来。用途是版权溯源与内容验证。
class ImgWatermarkPlugin : public QObject, public toolbox::IToolPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)

public:
    toolbox::ToolMeta meta() const override;
    QWidget *createPage(QWidget *parent) override;
};
