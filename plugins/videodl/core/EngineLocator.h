#pragma once

#include <QString>

// 视频下载插件里「去哪儿找内核可执行文件」的那部分纯逻辑。
//
// 定位规则是插件的核心约定之一（见 docs/architecture.md §8 的输出布局）：
// 手动指定 → 随程序目录 → PATH。把它抽出来是为了能脱离真实文件系统布局
// 单测这条优先级顺序，也为了不让「随程序目录」硬绑 QCoreApplication。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 内核的落地目录：<程序目录>/tools/bin。
///
/// 和插件 DLL 所在的 tools/ 同处一层，整个 bin/<Config>/ 拷给别人时内核
/// 也跟着走。appDir 由调用方传 QCoreApplication::applicationDirPath()，
/// 这样测试里可以喂任意目录。
QString engineDir(const QString &appDir);

/// 按「手动指定 → baseDir → PATH」的顺序定位一个可执行文件。
///
/// baseDir 传 engineDir() 的结果。返回空串表示没找到，调用方据此提示用户
/// 或降级（例如没有 ffmpeg 时退回单文件下载）。
QString resolveExecutable(const QString &manual, const QString &fileName, const QString &baseDir);

} // namespace videodl
