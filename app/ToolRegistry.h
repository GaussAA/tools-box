#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QPluginLoader;

namespace toolbox {
class IToolPlugin;
}

/// 插件仓库：扫描工具目录，装载所有实现了 toolbox::IToolPlugin 的 DLL。
///
/// 生命周期约定：ToolRegistry 负责持有 QPluginLoader，只要仓库不析构、
/// 不重新扫描，插件 DLL 就一直驻留在内存里。因此调用 rescan() 之前，
/// 必须先把由插件创建的页面全部销毁（见 MainWindow::reloadTools）。
class ToolRegistry : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        toolbox::IToolPlugin *plugin = nullptr;
        QString filePath;
    };

    explicit ToolRegistry(QObject *parent = nullptr);
    ~ToolRegistry() override;

    /// 插件目录约定：<exe 所在目录>/tools
    static QString defaultPluginDir();

    /// 清空已装载的插件并重新扫描指定目录，返回成功加载的工具数量。
    int rescan(const QString &dir = defaultPluginDir());

    const QList<Entry> &entries() const { return m_entries; }

    /// 上一次扫描中加载失败的插件及原因，用于在首页提示用户。
    QStringList errors() const { return m_errors; }

private:
    void unloadAll();

    QList<Entry> m_entries;
    QList<QPluginLoader *> m_loaders;
    QStringList m_errors;
};