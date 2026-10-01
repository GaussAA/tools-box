#pragma once

// 接口唯一标识宏：必须在任何使用处之前定义，供下方辅助函数与文末 Q_DECLARE_INTERFACE 共用。
#define ToolBoxPlugin_iid "com.toolbox.ToolBox/IToolPlugin/1.0"
#define ToolBoxToolPage_iid "com.toolbox.ToolBox/IToolPage/1.0"

// 工具箱插件 SDK：主程序与所有工具插件共同遵守的唯一契约。
// 这个头文件不参与编译成库，任何插件直接 include 即可。
//
// 注意：ToolMeta 是按值跨 DLL 传递的结构体，改动它的字段会破坏 ABI。
// 一旦改动，所有插件都必须用同一套 SDK 重新编译。

#include <QIcon>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>
#include <QWidget>
#include <QtPlugin>

#include <QJsonObject>

namespace toolbox {

/// 工具元信息。主程序用它来生成导航项、搜索索引和提示文字。
struct ToolMeta
{
    QString id;          ///< 全局唯一标识，建议 "分类.工具名"，如 "text.base64"
    QString name;        ///< 导航中显示的名字
    QString category;    ///< 分组名；留空则归入「其他」
    QString version;     ///< 插件版本号
    QString description; ///< 一句话说明，既显示为提示，也参与搜索匹配

    /// 导航图标。留空时外壳会用 name 的首字符自动生成一个占位图标，
    /// 所以这个字段是可选的。要自带图标，推荐在插件里挂资源：
    /// @code
    /// info.icon = QIcon(QStringLiteral(":/icons/my-tool.svg"));
    /// @endcode
    /// 也可以直接给 QStyle 的标准图标：@c QApplication::style()->standardIcon(...)。
    QIcon icon;
};

/// 工具专属的配置存储。
///
/// 主程序按 ToolMeta::id 自动加前缀隔离，所以插件之间不会串键，
/// 插件自己也不用操心命名空间：
/// @code
/// toolbox::ToolSettings settings(QStringLiteral("text.base64"));
/// settings.setValue("urlSafe", true);
/// const bool urlSafe = settings.value("urlSafe", false).toBool();
/// @endcode
///
/// 底层就是 QSettings（Windows 上落在注册表），值类型遵循 QVariant 的规则。
/// 想清空自己的全部配置用 clear()，不会影响别的工具。
class ToolSettings
{
public:
    explicit ToolSettings(QString toolId)
        : m_prefix(QStringLiteral("plugin/") + toolId + QLatin1Char('/'))
    {}

    QVariant value(const QString &key, const QVariant &defaultValue = QVariant()) const
    {
        return QSettings().value(m_prefix + key, defaultValue);
    }

    // 下面三个写入方法刻意声明为 const：它们改的是 QSettings 里的数据，
    // 而不是 ToolSettings 这个「句柄」本身。这样 saveState(const ToolSettings &)
    // 才能自然地写配置。
    void setValue(const QString &key, const QVariant &value) const
    {
        QSettings().setValue(m_prefix + key, value);
    }

    void remove(const QString &key) const { QSettings().remove(m_prefix + key); }

    /// 清空本工具的所有配置项。
    void clear() const
    {
        QSettings settings;
        settings.beginGroup(m_prefix);
        settings.remove(QString());
        settings.endGroup();
    }

private:
    QString m_prefix;
};

/// 工具页面可选实现的生命周期钩子。
///
/// 主程序在 createPage() 之后立刻调用 restoreState()，在重载插件或关闭窗口
/// 之前调用 saveState()。页面按需实现即可——不实现也不会出问题，只是拿不到
/// 自动存取的时机，得自己在构造/析构里读写 ToolSettings。
///
/// 页面要同时继承 QWidget 和本接口，并声明 Q_INTERFACES，
/// 主程序才能用 qobject_cast 认出它：
/// @code
/// class MyPage : public QWidget, public toolbox::IToolPage
/// {
///     Q_OBJECT
///     Q_INTERFACES(toolbox::IToolPage)
/// public:
///     void restoreState(const toolbox::ToolSettings &s) override;
///     void saveState(const toolbox::ToolSettings &s) override;
/// };
/// @endcode
class IToolPage
{
public:
    virtual ~IToolPage() = default;

    /// 恢复上次保存的状态。settings 已经按本工具的 id 隔离好。
    virtual void restoreState(const ToolSettings &settings) = 0;

    /// 保存当前状态。主程序在窗口关闭和重载插件前各调用一次。
    virtual void saveState(const ToolSettings &settings) = 0;
};

/// 工具箱插件接口。一个工具 = 一个实现本接口的 QObject 插件。
///
/// 写一个新插件的最小骨架：
/// @code
/// class MyPlugin : public QObject, public toolbox::IToolPlugin
/// {
///     Q_OBJECT
///     Q_PLUGIN_METADATA(IID ToolBoxPlugin_iid FILE "metadata.json")
///     Q_INTERFACES(toolbox::IToolPlugin)
/// public:
///     toolbox::ToolMeta meta() const override;
///     QWidget *createPage(QWidget *parent) override;
/// };
/// @endcode
///
/// 三个宏都不能少：Q_OBJECT 提供元对象，Q_INTERFACES 让主程序的
/// qobject_cast 能识别接口，Q_PLUGIN_METADATA 把接口 IID 写进 DLL 元数据。
///
/// 推荐（见 docs/best-practices-assessment.md §3.1）额外用
/// `FILE "metadata.json"` 把 ToolMeta 的纯数据字段静态嵌入 DLL，这样宿主可以在
/// 不加载插件的前提下枚举/过滤/校验，避免不兼容的 DLL 在 load() 时拖垮宿主。
class IToolPlugin
{
public:
    virtual ~IToolPlugin() = default;

    /// 工具的静态描述信息。
    ///
    /// **契约：必须无副作用，且同一次装载期间返回值保持稳定。** 主程序在装载
    /// 时调用一次并把结果缓存下来，之后的排序、导航、状态提示都读缓存，
    /// 不再重复调用。若要按外部状态改变返回值，必须换 id 走重新装载。
    virtual ToolMeta meta() const = 0;

    /// 创建工具页面。每次调用都应返回一个新实例，主程序接管其生命周期。
    /// 页面销毁前插件 DLL 不会被卸载，因此无需担心悬空指针。
    virtual QWidget *createPage(QWidget *parent = nullptr) = 0;
};

// === 静态插件元数据（docs/best-practices-assessment.md §3.1） =====================
//
// 插件在 Q_PLUGIN_METADATA(FILE "metadata.json") 里，把下面约定的字段挂到
// "toolbox" 这个 JSON 对象下；宿主用 QPluginLoader::metaData()（不调用 load()）
// 读取它来做兼容性门禁与快速枚举。

/// 插件静态元数据的根键。
inline constexpr char kPluginMetaKey[] = "toolbox";

/// 取插件静态元数据里的 `toolbox` 对象（即 metadata.json 的那一层）。
///
/// 取值得分两层，**少一层就永远拿到空结构**：`QPluginLoader::metaData()` 返回的对象
/// 里，`MetaData` 这个键才是 `Q_PLUGIN_METADATA(... FILE "metadata.json")` 所指文件的
/// 内容，而本工程的 metadata.json 顶层又有一个 `toolbox` 对象。
///
/// 抽成函数是为了让「JSON 形状」只有一处定义：宿主侧的门禁判定
/// （`app/core/PluginScanPolicy.h`）与本文件共用它，不需要再抄一遍键名。
inline QJsonObject pluginMetaObject(const QJsonObject &metaData)
{
    return metaData.value(QStringLiteral("MetaData"))
        .toObject()
        .value(QString::fromLatin1(kPluginMetaKey))
        .toObject();
}

/// 判断一个插件的静态元数据是否实现了当前版本的 IToolPlugin 接口。
///
/// **只比对 IID**，不加载插件，因此可安全地在 load() 之前筛掉「接口大版本不符」
/// 的 DLL。
///
/// 它**挡不住**另一种不兼容：IID 相同、却是用另一套 Qt / 编译器构建的 DLL ——
/// 那种 load() 得进去，然后因为二进制不兼容在宿主进程里崩。挡它要靠 `abi` 字段
/// （见 toolbox::evaluatePluginFields()，app/core/PluginScanPolicy.h），
/// 光有本函数不构成 ABI 防护。
inline bool isCompatiblePluginMetaData(const QJsonObject &metaData)
{
    return metaData.value(QStringLiteral("IID")).toString() == QStringLiteral(ToolBoxPlugin_iid);
}

/// 从插件的静态元数据里取 ToolMeta 的纯数据字段（不含 icon）。
///
/// 这个坑曾经真实存在：本函数只读了一层 `toolbox`，于是恒返回空结构；而测试构造了
/// 一份**假的扁平 JSON**（根键直接是 `toolbox`）去「验证」它，所以一直显示绿色。
/// 现在两层取值统一走 pluginMetaObject()，测试构造的也是与 Qt 真实输出一致的形状。
///
/// 目前只被测试使用：外壳的快速枚举路径尚未接入（`ToolRegistry::rescan` 只用门禁，
/// 名字与分类仍来自运行时的 meta()）。将来接入前要注意，metadata.json 里的
/// name/category/description 是**硬编码中文、不经过 `tr()`**，直接拿它渲染界面会让
/// 英文界面下显示中文（见 workflow §3.3）。
inline ToolMeta toolMetaFromMetaData(const QJsonObject &metaData)
{
    const QJsonObject tb = pluginMetaObject(metaData);
    ToolMeta m;
    m.id = tb.value(QStringLiteral("id")).toString();
    m.name = tb.value(QStringLiteral("name")).toString();
    m.category = tb.value(QStringLiteral("category")).toString();
    m.version = tb.value(QStringLiteral("version")).toString();
    m.description = tb.value(QStringLiteral("description")).toString();
    return m;
}

} // namespace toolbox

Q_DECLARE_INTERFACE(toolbox::IToolPlugin, ToolBoxPlugin_iid)
Q_DECLARE_INTERFACE(toolbox::IToolPage, ToolBoxToolPage_iid)
