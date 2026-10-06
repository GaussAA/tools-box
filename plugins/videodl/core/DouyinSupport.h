#pragma once

#include <QString>

// 视频下载插件里「抖音专线」的那部分纯逻辑。
//
// 抖音不能交给 yt-dlp 直连：它的 web detail 接口要签名，yt-dlp 至今没实现，
// 必然 403。这里只放与子进程、浏览器无关的判定与解析规则，让「什么算抖音
// 地址」「哪个画质档位对应哪个 ratio」「怎么从渲染好的 DOM 里取字段」这些
// 规则可以脱离界面单测（见 docs/architecture.md §3）。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 地址是不是抖音站点。大小写不敏感。
bool isDouyinUrl(const QString &url);

/// 画质下拉框下标 → 抖音播放接口的 ratio 参数。
///
/// 抖音只认这几档；「仅音频」也得先取到视频流再由 ffmpeg 抽音轨，所以同样
/// 按最高档拿。下标越界一律回落到最高档，不让界面传进来的脏值变成空串。
QString douyinRatio(int quality);

/// 从浏览器渲染出的 DOM 里取 video_id。取不到返回空串。
///
/// video_id 只有等抖音自己的页面脚本跑完才会出现在 DOM 里，所以这里的输入
/// 必须是渲染后的整份 DOM，不能是原始响应体。
QString parseDouyinVideoId(const QString &dom);

/// 从渲染出的 DOM 里取页面标题，并去掉「 - 抖音」后缀。取不到返回空串。
QString parseDouyinTitle(const QString &dom);

/// 渲染结局的分类。**顺序即优先级**，与页面里原先的 if 链一一对应。
///
/// 单独列出来的原因：这段判定原先埋在 DouyinResolver::onFinished 里，而它恰恰
/// 出过一次「真因被吃掉」的故障（渲染失败被报成 60 秒超时，见 docs/error_ledger.md
/// 第 4 条）。把顺序固化成可单测的规则，才不会再退化。
enum class DouyinRenderOutcome {
    Resolved,          ///< 正常结束且取到了 video_id
    Cancelled,         ///< 用户主动取消
    BrowserFailed,     ///< 浏览器起不来
    TimedOut,          ///< 渲染超时
    BrowserCrashed,    ///< 浏览器非零退出或异常终止
    NoVideoId          ///< 渲染完了但 DOM 里没有 video_id（抖音又改版了）
};

/// 按事实判定渲染结局。
///
/// 判定顺序：取消 → 启动失败 → 超时 → 退出异常 → 没有 video_id。
/// 前四条互相独立，最后一条只在「正常退出」时才可能被判。
DouyinRenderOutcome classifyDouyinRender(bool cancelled, bool startFailed, bool timedOut,
                                         bool exitedNormally, bool hasVideoId);

/// 用 video_id 与画质档位拼出播放接口地址。
///
/// 抽出来的理由：这条 URL 会被原样交给 yt-dlp 去取流，拼错（少了 line、ratio
/// 空串、参数顺序不对）的症状是「下载莫名失败」，与真正的原因隔着好几层。
QString douyinPlayUrl(const QString &videoId, int quality);

/// 从候选路径里挑一个能用的浏览器可执行文件；都不可用时返回空串。
///
/// 候选由调用方给（读环境变量是它的事，这里不碰环境）：跳过「以 / 开头」的
/// 路径 —— 环境变量缺失时会拼出 `/Microsoft/Edge/...` 这种绝对路径之外的怪串，
/// 不跳过就会去查一个根本不是路径的字符串。
QString pickHeadlessBrowser(const QStringList &candidates);

} // namespace videodl
