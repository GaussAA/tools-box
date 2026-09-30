#include "DouyinSupport.h"

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

} // namespace videodl
