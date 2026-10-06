#include "DouyinSupport.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QRegularExpressionMatch>

namespace videodl {

bool isDouyinUrl(const QString &url)
{
    return url.contains(QStringLiteral("douyin.com"), Qt::CaseInsensitive);
}

QString douyinRatio(int quality)
{
    switch (quality) {
    case 2:
        return QStringLiteral("720p");
    case 3:
        return QStringLiteral("540p");
    default:
        return QStringLiteral("1080p");
    }
}

QString parseDouyinVideoId(const QString &dom)
{
    static const QRegularExpression idRe(QStringLiteral(R"(video_id=([A-Za-z0-9]{16,}))"));
    return idRe.match(dom).captured(1);
}

QString parseDouyinTitle(const QString &dom)
{
    static const QRegularExpression titleRe(QStringLiteral(R"(<title>([^<]*)</title>)"));

    QString title = titleRe.match(dom).captured(1).trimmed();
    // 页面标题形如「<某条视频的标题> - 抖音」，把站点后缀去掉。
    const QString suffix = QStringLiteral(" - 抖音");
    if (title.endsWith(suffix)) {
        title.chop(suffix.size());
    }
    return title;
}

DouyinRenderOutcome classifyDouyinRender(bool cancelled, bool startFailed, bool timedOut,
                                         bool exitedNormally, bool hasVideoId)
{
    // 顺序与原先 DouyinResolver::onFinished 里的 if 链完全一致：取消最先看，
    // 因为「用户中止」与「出错」必须分开报；启动失败排在超时之前，否则浏览器
    // 根本起不来时会被误报成「60 秒超时」——那正是把真因吃掉的那次故障。
    if (cancelled) {
        return DouyinRenderOutcome::Cancelled;
    }
    if (startFailed) {
        return DouyinRenderOutcome::BrowserFailed;
    }
    if (timedOut) {
        return DouyinRenderOutcome::TimedOut;
    }
    if (!exitedNormally) {
        return DouyinRenderOutcome::BrowserCrashed;
    }
    return hasVideoId ? DouyinRenderOutcome::Resolved : DouyinRenderOutcome::NoVideoId;
}

QString douyinPlayUrl(const QString &videoId, int quality)
{
    return QStringLiteral("https://www.douyin.com/aweme/v1/play/?video_id=%1&ratio=%2&line=0")
        .arg(videoId, douyinRatio(quality));
}

QString pickHeadlessBrowser(const QStringList &candidates)
{
    for (const QString &path : candidates) {
        // 环境变量缺失时 qEnvironmentVariable 返回空串，拼出来的是
        // 「/Microsoft/Edge/Application/msedge.exe」——不以盘符开头，不是路径。
        if (path.startsWith(QLatin1Char('/'))) {
            continue;
        }
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return QString();
}

} // namespace videodl
