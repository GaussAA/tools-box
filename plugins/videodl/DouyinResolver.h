#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// 视频下载插件里「抖音专线」的那部分：借系统浏览器把页面渲染一遍，从 DOM 里取出
// video_id，再拼出播放直链（见 docs/architecture.md 计划 D2 的 DouyinResolver）。
//
// 为什么非走这条路不可：yt-dlp 的抖音实现打的是需要签名的 web detail 接口，而它的
// 源码里那行 TODO 说明签名压根没实现，必然 403 —— 换 cookies 也治不好。只能让浏览器
// 把页面脚本正常跑一遍，video_id 才会出现在 DOM 里。
//
// 它持有渲染用的 QProcess（所以不能进 core/）；判定与解析规则在 core/DouyinSupport。
class DouyinResolver : public QObject
{
    Q_OBJECT

public:
    explicit DouyinResolver(QObject *parent = nullptr);
    ~DouyinResolver() override;

    bool isRunning() const;

    /// 开始解析。quality 决定向播放接口要哪个 ratio 档位。
    void start(const QString &url, int quality);
    void cancel();

signals:
    void logLine(const QString &line);
    void status(const QString &text);
    /// 负数表示「没有百分比可报」（整个渲染阶段都没有）。
    void progress(int percent);
    /// 拿到直链了。title 可能为空（页面标题取不到时）。
    void resolved(const QString &playUrl, const QString &title);
    /// 失败或被取消。日志与状态文案已经自己发过了，页面只需复位按钮。
    void failed();

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    QProcess *m_process = nullptr;
    QByteArray m_dom;   ///< 浏览器吐出来的整份 DOM
    int m_quality = 0;
    bool m_timedOut = false;
    /// 浏览器压根没起来 —— 与「起来了但超时」是两回事，分开记才不会误报。
    bool m_startFailed = false;
    bool m_cancelled = false;
    int m_generation = 0; ///< 轮次编号，用来让过期的超时定时器失效
};
