#include "ToolRegistry.h"

#include "ToolBoxPlugin.h"
#include "core/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QPluginLoader>

#include <algorithm>

ToolRegistry::ToolRegistry(QObject *parent)
    : QObject(parent)
{}

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
        const QString dirError =
            tr("插件目录不存在：%1").arg(QDir::toNativeSeparators(dir));
        m_errors << dirError;
        toolbox::Logger::error(dirError);
        return 0;
    }

    const QStringList filters{QStringLiteral("*.dll"), QStringLiteral("*.so"),
                              QStringLiteral("*.dylib")};
    const QFileInfoList files = pluginDir.entryInfoList(filters, QDir::Files, QDir::Name);

    for (const QFileInfo &file : files) {
        auto *loader = new QPluginLoader(file.absoluteFilePath());

        // §3.1 静态元数据门禁：metaData() 只读取 DLL 内嵌的 JSON，不会把插件的
        // 代码加载进进程。这样即便某个 DLL 是用不兼容的 Qt/编译器版本构建、或根本
        // 不是工具箱插件，我们也能在调用 load() 之前识别并跳过，避免 load() 直接
        // 崩溃把宿主一起拖垮。IID 不符（含接口大版本变化）一律视为不兼容。
        const QJsonObject metaData = loader->metaData();
        if (!toolbox::isCompatiblePluginMetaData(metaData)) {
            const QString incompatible =
                tr("%1：不是兼容的工具箱插件（IID/接口版本不符，已跳过加载）。")
                    .arg(file.fileName());
            m_errors << incompatible;
            toolbox::Logger::warning(incompatible);
            delete loader;
            continue;
        }

        if (!loader->load()) {
            const QString loadError =
                QStringLiteral("%1：%2").arg(file.fileName(), loader->errorString());
            m_errors << loadError;
            toolbox::Logger::error(loadError);
            delete loader;
            continue;
        }

        QObject *root = loader->instance();
        auto *plugin = root ? qobject_cast<toolbox::IToolPlugin *>(root) : nullptr;
        if (!plugin) {
            const QString invalid =
                tr("%1：不是有效的工具箱插件（未实现 IToolPlugin）。").arg(file.fileName());
            m_errors << invalid;
            toolbox::Logger::error(invalid);
            loader->unload();
            delete loader;
            continue;
        }

        m_loaders.append(loader);

        Entry entry;
        entry.plugin = plugin;
        entry.filePath = file.absoluteFilePath();
        // meta() 在这里取一次就缓存住：它是跨 DLL 的虚调用，而排序比较器会被
        // 调用 O(n log n) 次，每次重取既浪费又危险。
        entry.meta = plugin->meta();
        m_entries.append(entry);

        toolbox::Logger::info(
            tr("已加载插件：%1（%2 %3）")
                .arg(file.fileName(), entry.meta.id, entry.meta.version));
    }

    // 按「分类 → 名称」排序，让同一类工具在导航里连续出现。
    // 未声明分类的工具统一排在最后。
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry &lhs, const Entry &rhs) {
        const bool aUncategorized = lhs.meta.category.isEmpty();
        const bool bUncategorized = rhs.meta.category.isEmpty();
        if (aUncategorized != bUncategorized) {
            return !aUncategorized;
        }
        if (lhs.meta.category != rhs.meta.category) {
            return lhs.meta.category < rhs.meta.category;
        }
        return lhs.meta.name.localeAwareCompare(rhs.meta.name) < 0;
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
