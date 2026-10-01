#include "PluginScanPolicy.h"

#include <QRegularExpression>

namespace toolbox {

bool isValidToolId(const QString &id)
{
    // 与 docs/architecture.md §4.1 的约定逐字对应：小写开头，至少带一个点分段。
    static const QRegularExpression idRe(QStringLiteral(R"(^[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*)+$)"));
    return idRe.match(id).hasMatch();
}

QString defaultHostAbi()
{
#ifdef TOOLBOX_HOST_ABI
    return QStringLiteral(TOOLBOX_HOST_ABI);
#else
    // 构建没注入指纹时返回空串：evaluatePluginFields 遇到空串一律拒绝，
    // 于是「忘了注入」表现为所有插件被拒（吵闹但明确），而不是静默放行一切。
    return QString();
#endif
}

PluginScanResult evaluatePluginFields(const QJsonObject &toolboxObject, const QString &hostAbi)
{
    PluginScanResult result;
    result.id = toolboxObject.value(QStringLiteral("id")).toString();
    result.actualAbi = toolboxObject.value(QStringLiteral("abi")).toString();
    result.expectedAbi = hostAbi;

    if (!isValidToolId(result.id)) {
        result.verdict = PluginVerdict::RejectId;
        return result;
    }

    // 指纹比对放在最后：id 不合法时先报那个，因为那才是插件作者能一眼看懂的原因。
    if (hostAbi.isEmpty() || result.actualAbi != hostAbi) {
        result.verdict = PluginVerdict::RejectAbi;
        return result;
    }

    result.verdict = PluginVerdict::Accept;
    return result;
}

} // namespace toolbox
