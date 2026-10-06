#include "core/EngineFetchPolicy.h"

namespace videodl {

// 判定顺序就是故障发生的顺序，每条都对应一次真实翻车：
//   1. 取消最先看 —— 用户主动中止不是失败，重试与报错都不该发生；
//   2. 传输成功但**带 Range 却收到 200**：服务端当没听见Range，从头给，
//      之前那半截已经对不上了，只能重头（继续拼会得到「长度对得上、
//      内容却是错」的文件，而且不报错）；
//   3. 收到 206 就必须核对服务端究竟从哪个字节开始给：仍可能从别的偏移给，
//      不核对就往旧的 .part 后面拼，同样是静默损坏；
//   4. 之后才是传输错误：还有次数就退避重试，用完才报失败。
// 注意第 2/3 条都要求「传输没有报错」——500、连接中断时 statusCode 可能是
// 任意值，此时应走重试/失败，而不是重头下。
FetchAction decideFetchAction(const FetchFacts &facts)
{
    if (facts.userCancelled) {
        return FetchAction::Cancelled;
    }

    if (!facts.transportFailed) {
        // 带 Range 却收到 200：服务端不支持续传。
        if (facts.offset > 0 && facts.statusCode == 200) {
            return FetchAction::RestartWhole;
        }
        // 206（Partial Content）：起点必须与已落盘的部分严丝合缝。
        if (facts.statusCode == 206
            && (facts.contentRangeStart < 0 || facts.contentRangeStart != facts.offset)) {
            return FetchAction::RestartWhole;
        }
    }

    if (facts.transportFailed) {
        return facts.attempt < facts.maxAttempts ? FetchAction::Retry : FetchAction::Fail;
    }

    return FetchAction::Complete;
}

} // namespace videodl
