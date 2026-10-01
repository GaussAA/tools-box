#include "ToolBoxPlugin.h"

#include <QJsonObject>
#include <QtTest>

/// 静态插件元数据辅助函数（docs/best-practices-assessment.md §3.1）的单元测试。
/// 只测纯逻辑，不需要加载任何真实插件 DLL。
class tst_pluginmeta : public QObject
{
    Q_OBJECT

private slots:
    void isCompatiblePluginMetaData_acceptsCurrentIid();
    void isCompatiblePluginMetaData_rejectsWrongIid();
    void isCompatiblePluginMetaData_rejectsEmpty();
    void toolMetaFromMetaData_parsesToolboxObject();
};

void tst_pluginmeta::isCompatiblePluginMetaData_acceptsCurrentIid()
{
    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral(ToolBoxPlugin_iid));
    QVERIFY(toolbox::isCompatiblePluginMetaData(md));
}

void tst_pluginmeta::isCompatiblePluginMetaData_rejectsWrongIid()
{
    // 接口大版本升级后，旧 DLL 的 IID 会不同，应当被门禁拦下而非加载崩溃。
    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral("com.toolbox.ToolBox/IToolPlugin/2.0"));
    QVERIFY(!toolbox::isCompatiblePluginMetaData(md));
}

void tst_pluginmeta::isCompatiblePluginMetaData_rejectsEmpty()
{
    // 非 Qt 插件或已损坏的二进制，metaData() 通常返回空对象，IID 缺失即不兼容。
    QVERIFY(!toolbox::isCompatiblePluginMetaData(QJsonObject{}));
}

void tst_pluginmeta::toolMetaFromMetaData_parsesToolboxObject()
{
    QJsonObject tb;
    tb.insert(QStringLiteral("id"), QStringLiteral("text.base64"));
    tb.insert(QStringLiteral("name"), QStringLiteral("Base64 编解码"));
    tb.insert(QStringLiteral("category"), QStringLiteral("文本编码"));
    tb.insert(QStringLiteral("version"), QStringLiteral("0.2.0"));
    tb.insert(QStringLiteral("description"), QStringLiteral("UTF-8 文本与 Base64 互转。"));

    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral(ToolBoxPlugin_iid));
    md.insert(QStringLiteral("toolbox"), tb);

    const toolbox::ToolMeta m = toolbox::toolMetaFromMetaData(md);
    QCOMPARE(m.id, QStringLiteral("text.base64"));
    QCOMPARE(m.name, QStringLiteral("Base64 编解码"));
    QCOMPARE(m.category, QStringLiteral("文本编码"));
    QCOMPARE(m.version, QStringLiteral("0.2.0"));
    QCOMPARE(m.description, QStringLiteral("UTF-8 文本与 Base64 互转。"));
    QVERIFY(m.icon.isNull()); // 静态元数据不含图标，由运行时 meta() 提供
}

QTEST_GUILESS_MAIN(tst_pluginmeta)

#include "tst_pluginmeta.moc"
