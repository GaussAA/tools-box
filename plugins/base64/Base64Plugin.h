#pragma once

#include "ToolBoxPlugin.h"

#include <QObject>

/// 示例插件：Base64 编解码。
/// 它同时是「怎么写一个新工具」的模板 —— 复制这个目录改个名字就能开工。
/// 页面类 Base64Page 定义在 .cpp 里（含 Q_OBJECT，靠 Base64Plugin.moc 走 moc）。
class Base64Plugin : public QObject, public toolbox::IToolPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)

public:
    toolbox::ToolMeta meta() const override;
    QWidget *createPage(QWidget *parent) override;
};
