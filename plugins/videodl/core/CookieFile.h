#pragma once

#include <QString>

// 视频下载插件里「把浏览器导出的 cookies.txt 收拾干净」的那部分纯逻辑。
//
// 浏览器扩展导出 Netscape 格式时经常犯两个错，而 yt-dlp（走 Python 的
// cookiejar）对这两处很严格，一不合规就拒收整个文件，报一句
// 「invalid Netscape format cookies file」，让人摸不着头脑。
// 规则本身与界面无关，抽出来是为了能用临时目录直接单测（见
// docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 规范化副本的默认落地路径（在当前用户的临时目录里）。
///
/// 这份副本含有登录凭据，调用方必须在任务结束后删除它。
QString normalizedCookiesPath();

/// 把浏览器导出的 cookies.txt 收拾干净，写到 target。
///
/// 修掉两处让 cookiejar 直接抛异常的毛病：
///   1. 域名以「.」开头（表示对子域也生效），includeSubDomains 列却写成
///      FALSE —— cookiejar 里有断言要求两者一致，直接抛 AssertionError。
///   2. 少量畸形行，cookie 名字是空的。
/// 统一处理方式：补齐第 2 列、丢掉坏行、写一份干净的目标文件。
/// 原文件本身没问题时也照走一遍，免得行为时好时坏。
///
/// target 显式传入而不是内部写死，测试才好用 QTemporaryDir 接住产物。
/// 返回实际写入的路径；读取或写入失败时返回空串。
/// 修正/丢弃的行数通过出参带回，好写进日志。
QString normalizeCookies(const QString &source, const QString &target, int *fixedRows,
                         int *droppedRows);

} // namespace videodl
