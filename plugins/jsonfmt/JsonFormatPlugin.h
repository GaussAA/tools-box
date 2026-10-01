#pragma once

#include "ToolBoxPlugin.h"

#include <QObject>

/// 示例插件：JSON 格式化。
/// 它故意不设置 ToolMeta::icon，用来验证外壳的首字符占位图标。
class JsonFormatPlugin : public QObject, public toolbox::IToolPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)

public:
    toolbox::ToolMeta meta() const override;
    QWidget *createPage(QWidget *parent) override;
};
