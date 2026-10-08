#include "ToolBoxPlugin.h"

#include <QJsonObject>
#include <QtTest>

/// 静态插件元数据辅助函数（docs/best-practices-assessment.md §3.1）的单元测试。
/// 只测纯逻辑，不需要加载任何真实插件 DLL。
class TestPluginMetaData : public QObject
{
    Q_OBJECT

private slots:
    void isCompatiblePluginMetaDataAcceptsCurrentIid();
    void isCompatiblePluginMetaDataRejectsWrongIid();
    void isCompatiblePluginMetaDataRejectsEmpty();
    void toolMetaFromMetaDataParsesToolboxObject();
};

void TestPluginMetaData::isCompatiblePluginMetaDataAcceptsCurrentIid()
{
    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral(ToolBoxPlugin_iid));
    QVERIFY(toolbox::isCompatiblePluginMetaData(md));
}

void TestPluginMetaData::isCompatiblePluginMetaDataRejectsWrongIid()
{
    // 接口大版本升级后，旧 DLL 的 IID 会不同，应当被门禁拦下而非加载崩溃。
    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral("com.toolbox.ToolBox/IToolPlugin/2.0"));
    QVERIFY(!toolbox::isCompatiblePluginMetaData(md));
}

void TestPluginMetaData::isCompatiblePluginMetaDataRejectsEmpty()
{
    // 非 Qt 插件或已损坏的二进制，metaData() 通常返回空对象，IID 缺失即不兼容。
    QVERIFY(!toolbox::isCompatiblePluginMetaData(QJsonObject{}));
}

// 这里构造的 JSON 必须与 QPluginLoader::metaData() 的真实输出同形：metadata.json 的
// 内容被 Qt 放在 "MetaData" 键下，而本工程的 metadata.json 顶层再有 "toolbox" 对象。
//
// 这个形状本身就是被测契约的一部分：曾经这里构造的是**扁平对象**（根键直接是
// toolbox），于是函数少读一层也能通过，缺陷长期显示为绿 —— 别再退回那种形状。
void TestPluginMetaData::toolMetaFromMetaDataParsesToolboxObject()
{
    QJsonObject tb;
    tb.insert(QStringLiteral("id"), QStringLiteral("text.base64"));
    tb.insert(QStringLiteral("name"), QStringLiteral("Base64 编解码"));
    tb.insert(QStringLiteral("category"), QStringLiteral("文本编码"));
    tb.insert(QStringLiteral("version"), QStringLiteral("0.2.0"));
    tb.insert(QStringLiteral("description"), QStringLiteral("UTF-8 文本与 Base64 互转。"));

    // 第一层：metadata.json 文件本身，顶层就是 kPluginMetaKey 指的那个对象。
    QJsonObject metadataFile;
    metadataFile.insert(QString::fromLatin1(toolbox::kPluginMetaKey), tb);

    // 第二层：Qt 把整个 metadata.json 的内容放进 "MetaData"。少写这一层，
    // 测试就会退化成「构造一个函数实际读不到的形状」，把缺陷放过去。
    QJsonObject md;
    md.insert(QStringLiteral("IID"), QStringLiteral(ToolBoxPlugin_iid));
    md.insert(QStringLiteral("className"), QStringLiteral("Base64Plugin"));
    md.insert(QStringLiteral("MetaData"), metadataFile);

    const toolbox::ToolMeta m = toolbox::toolMetaFromMetaData(md);
    QCOMPARE(m.id, QStringLiteral("text.base64"));
    QCOMPARE(m.name, QStringLiteral("Base64 编解码"));
    QCOMPARE(m.category, QStringLiteral("文本编码"));
    QCOMPARE(m.version, QStringLiteral("0.2.0"));
    QCOMPARE(m.description, QStringLiteral("UTF-8 文本与 Base64 互转。"));
    QVERIFY(m.icon.isNull()); // 静态元数据不含图标，由运行时 meta() 提供

    // 反例：少了 Qt 包上的那一层（只给 metadata.json 本身），就应当一个字段都取不到。
    QVERIFY(toolbox::toolMetaFromMetaData(metadataFile).id.isEmpty());
}

QTEST_GUILESS_MAIN(TestPluginMetaData)

#include "tst_pluginmeta.moc"
