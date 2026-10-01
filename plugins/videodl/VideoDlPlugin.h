#pragma once

#include "ToolBoxPlugin.h"

#include <QObject>

/// 视频下载插件：粘贴主流视频站点（B站 / YouTube / 抖音 等）的地址，
/// 按画质下载到本地。下载内核是 yt-dlp 独立可执行文件——插件本身
/// 不依赖 Python，也不需要全局安装任何东西。
class VideoDlPlugin : public QObject, public toolbox::IToolPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
    Q_INTERFACES(toolbox::IToolPlugin)

public:
    toolbox::ToolMeta meta() const override;
    QWidget *createPage(QWidget *parent) override;
};
