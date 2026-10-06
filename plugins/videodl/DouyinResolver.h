#pragma once

#include "IChildProcess.h"

#include <QObject>
#include <QString>
#include <QStringList>

// 视频下载插件里「抖音专线」的那部分：借系统浏览器把页面渲染一遍，从 DOM 里取出
// video_id，再拼出播放直链（见 docs/architecture.md 计划 D2 的 DouyinResolver）。
//
// 为什么非走这条路不可：yt-dlp 的抖音实现打的是需要签名的 web detail 接口，而它的
// 源码里那行 TODO 说明签名压根没实现，必然 403 —— 换 cookies 也治不好。只能让浏览器
// 把页面脚本正常跑一遍，video_id 才会出现在 DOM 里。
//
// 判定与解析规则在 core/DouyinSupport；浏览器候选的**挑选规则**也在 core，这里只负责
// 读环境变量拼出候选路径。子进程经 IChildProcess 注入，测试可以不真起浏览器。
class DouyinResolver : public QObject
{
    Q_OBJECT

public:
    /// 生产用法：自己持有一个真实的子进程，浏览器候选从环境变量拼。
    explicit DouyinResolver(QObject *parent = nullptr);

    /// 测试用法：注入子进程（所有权不转移，须活到本对象销毁之后）。
    explicit DouyinResolver(IChildProcess *process, QObject *parent = nullptr);

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

protected:
    /// 浏览器可执行文件的候选路径。
    ///
    /// 做成虚函数而不是私有函数，是为了让测试能给出一台「确实装了浏览器」的机器：
    /// 生产实现读环境变量，而测试进程里的 ProgramFiles 未必指向任何真实目录，
    /// 不覆盖它的话每个用例都会先撞上「未找到浏览器」这条分支，根本走不到渲染逻辑。
    virtual QStringList browserCandidates() const;

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    IChildProcess *m_process = nullptr;
    bool m_ownsProcess = false;
    QByteArray m_dom;   ///< 浏览器吐出来的整份 DOM
    int m_quality = 0;
    bool m_timedOut = false;
    /// 浏览器压根没起来 —— 与「起来了但超时」是两回事，分开记才不会误报。
    bool m_startFailed = false;
    bool m_cancelled = false;
    int m_generation = 0; ///< 轮次编号，用来让过期的超时定时器失效
};
