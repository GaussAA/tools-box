#include "core/PluginScanPolicy.h"

#include <QJsonObject>
#include <QTest>

// 插件装载门禁的判定规则（app/core/PluginScanPolicy）。
//
// 这些分支以前只能靠「真往 tools/ 里放一个坏 DLL」去验证，现在喂一个 JSON 就能测。
class TestPluginScanPolicy : public QObject
{
    Q_OBJECT

private slots:
    void isValidToolIdAcceptsNamespacedIds();
    void isValidToolIdRejectsMalformedIds();
    void evaluateAcceptsMatchingAbi();
    void evaluateRejectsMalformedId();
    void evaluateRejectsAbiMismatch();
    void evaluateRejectsMissingAbi();
    void evaluateRejectsUnknownHost();
    void evaluatePrefersIdErrorOverAbiError();
    void defaultHostAbiIsInjected();
};

namespace {

/// 造一个 metadata.json 里 `toolbox` 那一层的对象。
QJsonObject makeToolbox(const QString &id, const QString &abi)
{
    QJsonObject tb;
    tb.insert(QStringLiteral("id"), id);
    tb.insert(QStringLiteral("abi"), abi);
    return tb;
}

const QString kHostAbi = QStringLiteral("6.12.0-MSVC-144");

} // namespace

void TestPluginScanPolicy::isValidToolIdAcceptsNamespacedIds()
{
    // 架构 §4.1 的三个现役 id 都必须是合法的，否则门禁会把自己人挡在门外。
    QVERIFY(toolbox::isValidToolId(QStringLiteral("text.base64")));
    QVERIFY(toolbox::isValidToolId(QStringLiteral("dev.json-format")));
    QVERIFY(toolbox::isValidToolId(QStringLiteral("media.video-download")));
}

void TestPluginScanPolicy::isValidToolIdRejectsMalformedIds()
{
    QVERIFY(!toolbox::isValidToolId(QString()));              // 空
    QVERIFY(!toolbox::isValidToolId(QStringLiteral("base64"))); // 没有分类前缀
    QVERIFY(!toolbox::isValidToolId(QStringLiteral("Text.Base64"))); // 大写
    QVERIFY(!toolbox::isValidToolId(QStringLiteral("1text.base64"))); // 数字开头
    QVERIFY(!toolbox::isValidToolId(QStringLiteral("text.")));  // 空的第二段
}

void TestPluginScanPolicy::evaluateAcceptsMatchingAbi()
{
    const toolbox::PluginScanResult result =
        toolbox::evaluatePluginFields(makeToolbox(QStringLiteral("text.base64"), kHostAbi),
                                      kHostAbi);

    QCOMPARE(result.verdict, toolbox::PluginVerdict::Accept);
    QCOMPARE(result.id, QStringLiteral("text.base64"));
}

void TestPluginScanPolicy::evaluateRejectsMalformedId()
{
    const toolbox::PluginScanResult result =
        toolbox::evaluatePluginFields(makeToolbox(QStringLiteral("Base64"), kHostAbi), kHostAbi);

    QCOMPARE(result.verdict, toolbox::PluginVerdict::RejectId);
    QCOMPARE(result.id, QStringLiteral("Base64"));
}

void TestPluginScanPolicy::evaluateRejectsAbiMismatch()
{
    // 这才是 IID 门禁挡不住的那种：接口版本对，工具链不对。
    const toolbox::PluginScanResult result = toolbox::evaluatePluginFields(
        makeToolbox(QStringLiteral("text.base64"), QStringLiteral("6.10.3-MSVC-143")), kHostAbi);

    QCOMPARE(result.verdict, toolbox::PluginVerdict::RejectAbi);
    QCOMPARE(result.actualAbi, QStringLiteral("6.10.3-MSVC-143"));
    QCOMPARE(result.expectedAbi, kHostAbi);
}

void TestPluginScanPolicy::evaluateRejectsMissingAbi()
{
    // 没有指纹 = 不是这套工具链产出的，一律拒绝：门禁的意义就是挡来源不明的 DLL。
    const toolbox::PluginScanResult result =
        toolbox::evaluatePluginFields(makeToolbox(QStringLiteral("text.base64"), QString()),
                                      kHostAbi);

    QCOMPARE(result.verdict, toolbox::PluginVerdict::RejectAbi);
    QVERIFY(result.actualAbi.isEmpty());
}

void TestPluginScanPolicy::evaluateRejectsUnknownHost()
{
    // 宿主自己没拿到指纹（构建忘了注入）时也必须拒绝，而不是放行一切。
    const toolbox::PluginScanResult result =
        toolbox::evaluatePluginFields(makeToolbox(QStringLiteral("text.base64"), kHostAbi),
                                      QString());

    QCOMPARE(result.verdict, toolbox::PluginVerdict::RejectAbi);
}

void TestPluginScanPolicy::evaluatePrefersIdErrorOverAbiError()
{
    // id 不合法 + abi 也不对时，报 id：那才是插件作者能一眼看懂的原因。
    const toolbox::PluginScanResult result = toolbox::evaluatePluginFields(
        makeToolbox(QStringLiteral("Base64"), QStringLiteral("other")), kHostAbi);

    QCOMPARE(result.verdict, toolbox::PluginVerdict::RejectId);
}

void TestPluginScanPolicy::defaultHostAbiIsInjected()
{
    // 构建忘了注入 TOOLBOX_HOST_ABI 时会返回空串，后果是所有插件都被自家门禁拒掉。
    // 这条断言就是为了让它变成一次测试失败，而不是用户那边「一个工具都没有」。
    QVERIFY2(!toolbox::defaultHostAbi().isEmpty(),
             "TOOLBOX_HOST_ABI 未注入：检查顶层 CMakeLists.txt 的 TOOLBOX_PLUGIN_ABI");
}

QTEST_APPLESS_MAIN(TestPluginScanPolicy)

#include "tst_pluginscanpolicy.moc"
