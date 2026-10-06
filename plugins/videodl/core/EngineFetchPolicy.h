#pragma once

#include <QtGlobal>

// 视频下载插件里「一次内核文件传输结束后，下一步该怎么办」的纯逻辑。
//
// 这段决策原先埋在 EngineFetcher::onReplyFinished 里：它是全项目最容易出错的
// 一小段——带 Range 却收到 200（服务端不支持续传）、206 的起点与已落盘的部分
// 对不上、重试次数用尽、用户中途点了取消——每一条都对应过一次真实故障
// （错误台账把「症状与根因隔得很远」的教训都记在这里面）。埋在 QObject 里
// 只能靠真下载去验证，core 抽出来后就能用表驱动逐条钉住（tests/tst_enginefetchpolicy.cpp）。
//
// 刻意只接收「事实」而不是 QNetworkReply：videodl_core 只链接 Qt6::Core
// （分层判定见 docs/architecture.md §3），引入 QtNetwork 就成了违规依赖。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 一次传输结束后的处置结论。
enum class FetchAction {
    /// 传输正常结束，可以把 .part 落地。
    Complete,
    /// 服务端不支持续传、或续传起点与已落盘的部分对不上：删掉半截重头再来。
    RestartWhole,
    /// 传输失败但还有次数：退避后重试。
    Retry,
    /// 次数用尽：报错收尾。
    Fail,
    /// 用户主动取消：不重试、也不报失败（取消与失败必须分开，见架构 §7）。
    Cancelled,
};

/// 做决策所需的全部事实。缺省值即「一切正常」，便于只填异常项。
struct FetchFacts
{
    /// 用户点了取消。**优先级最高**：取消时哪怕同时有传输错误也不报失败。
    bool userCancelled = false;
    /// 没有回复，或 error != NoError。
    bool transportFailed = false;
    /// HTTP 状态码；0 表示没有回复。
    int statusCode = 0;
    /// .part 里已落盘的字节数（0 = 本次是全量下载）。
    qint64 offset = 0;
    /// Content-Range 的起始字节；-1 表示响应里没有或不可解析。
    qint64 contentRangeStart = -1;
    /// 已经尝试了几次（含本次）。
    int attempt = 1;
    /// 最多允许尝试几次。
    int maxAttempts = 5;
};

/// 按事实决定下一步。纯函数：不碰文件、不发信号、不读时钟。
FetchAction decideFetchAction(const FetchFacts &facts);

} // namespace videodl
