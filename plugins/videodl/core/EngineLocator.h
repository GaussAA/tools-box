#pragma once

#include <QString>

// 视频下载插件里「去哪儿找内核可执行文件」的那部分纯逻辑。
//
// 定位规则是插件的核心约定之一（见 docs/architecture.md §8 的输出布局）：
// 手动指定 → 随程序目录 → PATH → 共享目录。把它们抽出来是为了能脱离真实
// 文件系统布局单测这条优先级顺序，也为了不让「随程序目录」硬绑
// QCoreApplication。
//
// 共享目录是 2026-10-06 加的最后一级兜底：外部内核体积大，同一台机器上多个工具
// 都要用，各自下载一份既费磁盘又容易版本不一致；用户填一次目录，所有插件都受益。
// 它排在 PATH 之后 —— 前三级都是「本机本来就有的」，共享目录是「用户特意指出来的」，
// 放最后可以在前面的来源可用时不打扰用户。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 内核的落地目录：<程序目录>/tools/bin。
///
/// 和插件 DLL 所在的 tools/ 同处一层，整个 bin/<Config>/ 拷给别人时内核
/// 也跟着走。appDir 由调用方传 QCoreApplication::applicationDirPath()，
/// 这样测试里可以喂任意目录。
QString engineDir(const QString &appDir);

/// 按「手动指定 → baseDir → PATH → sharedDir」的顺序定位一个可执行文件。
///
/// baseDir 传 engineDir() 的结果；sharedDir 是用户配置的共享目录，可为空
/// （为空时行为与从前完全一致）。返回空串表示没找到，调用方据此提示用户
/// 或降级（例如没有 ffmpeg 时退回单文件下载）。
QString resolveExecutable(const QString &manual, const QString &fileName, const QString &baseDir,
                          const QString &sharedDir = QString());

} // namespace videodl
