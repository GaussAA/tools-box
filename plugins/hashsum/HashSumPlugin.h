#pragma once

#include "ToolBoxPlugin.h"

#include <QObject>

/// 工具插件：哈希与校验和（MD5 / SHA-1 / SHA-256 / SHA-512）。
/// 目录结构照 plugins/base64/ 复制而来 —— 那是「怎么写一个新工具」的模板。
/// 页面类 HashSumPage 定义在 .cpp 里（含 Q_OBJECT，靠 HashSumPlugin.moc 走 moc）。
class HashSumPlugin : public QObject, public toolbox::IToolPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)

public:
    toolbox::ToolMeta meta() const override;
    QWidget *createPage(QWidget *parent) override;
};
