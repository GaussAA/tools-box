#include "ToolRegistry.h"

#include "ToolBoxPlugin.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QPluginLoader>

#include <algorithm>

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{
}

ToolRegistry::~ToolRegistry()
{
    unloadAll();
}

QString ToolRegistry::defaultPluginDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/tools");
}

int ToolRegistry::rescan(const QString &dir)
{
    unloadAll();

    const QDir pluginDir(dir);
    if (!pluginDir.exists()) {
        m_errors << tr("插件目录不存在：%1").arg(QDir::toNativeSeparators(dir));
        return 0;
    }

    const QStringList filters{QStringLiteral("*.dll"), QStringLiteral("*.so"), QStringLiteral("*.dylib")};
    const QFileInfoList files = pluginDir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo &file : files) {
        auto *loader = new QPluginLoader(file.absoluteFilePath());
        if (!loader->load()) {
            m_errors << QStringLiteral("%1：%2").arg(file.fileName(), loader->errorString());
            delete loader;
            continue;
        }

        QObject *root = loader->instance();
        auto *plugin = root ? qobject_cast<toolbox::IToolPlugin *>(root) : nullptr;
        if (!plugin) {
            m_errors << tr("%1：不是有效的工具箱插件（未实现 IToolPlugin）。")
                            .arg(file.fileName());
            loader->unload();
            delete loader;
            continue;
        }

        m_loaders.append(loader);
        m_entries.append(Entry{plugin, file.absoluteFilePath()});
    }

    // 按「分类 → 名称」排序，让同一类工具在导航里连续出现。
    // 未声明分类的工具统一排在最后。
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry &lhs, const Entry &rhs) {
        const toolbox::ToolMeta a = lhs.plugin->meta();
        const toolbox::ToolMeta b = rhs.plugin->meta();
        const bool aUncategorized = a.category.isEmpty();
        const bool bUncategorized = b.category.isEmpty();
        if (aUncategorized != bUncategorized) {
            return !aUncategorized;
        }
        if (a.category != b.category) {
            return a.category < b.category;
        }
        return a.name.localeAwareCompare(b.name) < 0;
    });

    return static_cast<int>(m_entries.size());
}

void ToolRegistry::unloadAll()
{
    for (Entry &entry : m_entries) {
        entry.plugin = nullptr;
    }
    m_entries.clear();

    for (QPluginLoader *loader : m_loaders) {
        loader->unload();
        delete loader;
    }
    m_loaders.clear();

    m_errors.clear();
}