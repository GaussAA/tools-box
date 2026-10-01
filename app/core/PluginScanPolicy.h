#pragma once

#include <QJsonObject>
#include <QString>

// 插件扫描的「放行还是拒绝」判定：只看数据，不碰 QPluginLoader、不碰 QWidget。
//
// 抽出来的理由与 ToolCatalog 一样（docs/architecture.md §3）：这段规则以前混在
// ToolRegistry::rescan 的循环里，想验证「某个插件为什么被跳过」就得真的造一个
// DLL 放进去。这里把它变成「喂一个 JSON、得到一个判定」，于是每条分支都能单测。
//
// 输入是已经取到 `toolbox` 那一层之后的 JSON 对象 —— 取 JSON 的形状是
// sdk/ToolBoxPlugin.h 的事（那里才有键名常量），本文件不重复那份约定。
//
// 本文件不得 include 任何 QtWidgets 头文件。

namespace toolbox {

/// 扫描一个插件的结论。
enum class PluginVerdict {
    Accept,   ///< 放行，可以安全地 load()
    RejectId, ///< id 缺失或不符合「分类.工具名」的约定（架构 §4.1）
    RejectAbi ///< 构建环境指纹与宿主不一致，load() 极可能把宿主一起拖崩
};

struct PluginScanResult
{
    PluginVerdict verdict = PluginVerdict::Accept;
    QString id;          ///< 插件自带的 id（被拒时用来报「是哪个 id 不合法」）
    QString expectedAbi; ///< 宿主期望的构建环境指纹
    QString actualAbi;   ///< 插件自带的构建环境指纹
};

/// id 是否符合 `分类.工具名` 的约定（`^[a-z][a-z0-9]*(\.[a-z][a-z0-9-]*)+$`）。
///
/// 架构 §4.1 规定了这条格式，但光写在文档里等于没有：id 同时是配置键前缀与
/// 收藏/最近使用的持久化依据，一个畸形 id 会把配置命名空间搅乱。
bool isValidToolId(const QString &id);

/// 判定一个插件的静态元数据能不能放行。
///
/// `hostAbi` 传 `defaultHostAbi()`，测试里可以喂任意值来验证「不一致时会被拒」。
/// **abi 缺失一律拒绝**：门禁存在的意义就是挡住「来源不明的 DLL」，
/// 缺了指纹正好说明这份 DLL 不是本工程这套工具链产出的。
PluginScanResult evaluatePluginFields(const QJsonObject &toolboxObject, const QString &hostAbi);

/// 本程序（宿主）自己的构建环境指纹。
///
/// 由 CMake 在构建时注入（`TOOLBOX_HOST_ABI`，见顶层 CMakeLists.txt），与写进各插件
/// metadata.json 的 `abi` 字段同源 —— 两边必须同源，否则每个插件都会被自己拒掉。
QString defaultHostAbi();

} // namespace toolbox
