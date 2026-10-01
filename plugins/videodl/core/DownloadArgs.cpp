#include "DownloadArgs.h"

namespace videodl {

QStringList buildYtDlpArgs(const DownloadSpec &spec)
{
    QStringList args;
    args << QStringLiteral("--newline")    // 让进度按行刷新，才好解析
         << QStringLiteral("--no-playlist") // 只下载当前这一个视频
         << QStringLiteral("-P") << spec.outputDir;

    if (spec.title.isEmpty()) {
        args << QStringLiteral("-o") << QStringLiteral("%(title)s [%(id)s].%(ext)s");
    } else {
        // 只有抖音那条路会传标题：拿到的是直链，generic extractor 只会把文件叫成
        // video.mp4，只好把标题写进输出模板。百分号在模板里有含义，先转义掉。
        const QString escaped = QString(spec.title).replace(QLatin1Char('%'), QStringLiteral("%%"));
        args << QStringLiteral("-o") << (escaped + QStringLiteral(".%(ext)s"));
    }

    if (spec.needsReferer) {
        // 抖音的 CDN 会检查来源，少这个头就会 403。
        args << QStringLiteral("--referer") << QStringLiteral("https://www.douyin.com/");
    }

    if (!spec.cookiesPath.isEmpty()) {
        // 部分 B站 会员内容、YouTube 年龄限制内容需要浏览器 cookies。
        args << QStringLiteral("--cookies") << spec.cookiesPath;
    }

    if (spec.audioOnly) {
        args << QStringLiteral("-x") << QStringLiteral("--audio-format") << QStringLiteral("mp3");
    } else if (!spec.hasFfmpeg) {
        // 没有 ffmpeg 就没法合并音视频分轨，退回「最佳单文件」。
        // 代价是清晰度通常只有 360p/480p。
        args << QStringLiteral("-f") << QStringLiteral("b");
    } else {
        args << QStringLiteral("--merge-output-format") << QStringLiteral("mp4");
        switch (spec.quality) {
        case 1:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=1080]+ba/b[height<=1080]");
            break;
        case 2:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=720]+ba/b[height<=720]");
            break;
        case 3:
            args << QStringLiteral("-f") << QStringLiteral("bv*[height<=480]+ba/b[height<=480]");
            break;
        default:
            args << QStringLiteral("-f") << QStringLiteral("bv*+ba/b");
            break;
        }
    }

    if (spec.hasFfmpeg) {
        // 只传目录即可，yt-dlp 会自己去里面找 ffmpeg。
        args << QStringLiteral("--ffmpeg-location") << spec.ffmpegDir;
    }

    args << spec.url;
    return args;
}

} // namespace videodl
