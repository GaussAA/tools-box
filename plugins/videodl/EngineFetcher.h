#pragma once

#include "IChildProcess.h"
#include "IEngineTransport.h"

#include <QObject>
#include <QString>

class QDirIterator;

/// 下载内核（yt-dlp / ffmpeg）的那部分：发起传输、断点续传、解压、取消。
///
/// 它原先是 VideoDlPage 的一部分（见 docs/architecture.md 计划 D2）。拆出来的直接
/// 理由是页面被撑到 1300 行以上、撞上了 `verify_filesize.ps1` 的豁免上限；真正的
/// 理由是职责：页面只该管界面，而「什么时候该续传、什么时候该重来、怎么算取消」
/// 与界面毫无关系。
///
/// 与页面之间只靠信号说话。网络经 IEngineTransport 注入、解压子进程经
/// IChildProcess 注入：两条外部通道都可以换成假的，于是状态机（续传 / 重试 / 取消
/// / 放弃）不必真下载也能验证 —— 剩下真正只属于真机的，只有「解压出来的是不是
/// ffmpeg.exe」这一段。
class EngineFetcher : public QObject
{
    Q_OBJECT

public:
    /// 正在进行的下载任务。同一时刻只允许一个，所以不是「几个 bool」。
    enum class Kind { None, YtDlp, Ffmpeg };

    /// 生产用法：真实网络 + 真实解压子进程。
    explicit EngineFetcher(QObject *parent = nullptr);

    /// 测试用法：注入网络与解压子进程。**所有权都不转移**，调用方须让它们活到
    /// 本对象销毁之后。
    explicit EngineFetcher(IEngineTransport *transport, IChildProcess *unzip,
                           QObject *parent = nullptr);

    ~EngineFetcher() override;

    bool isBusy() const { return m_kind != Kind::None; }

    /// 下载 url 到 targetPath。已有同名文件会被覆盖；中途失败会带断点续传重试。
    void start(Kind kind, const QString &url, const QString &targetPath);

    /// 用户主动取消。**取消不是失败**：不会走重试，也不报「下载失败」。
    void cancel();

signals:
    void logLine(const QString &line); ///< 一行日志，交给页面显示
    void progress(int percent);        ///< 0–100；负数表示「没有百分比」（不确定态）
    void status(const QString &text);  ///< 当前阶段的一行说明
    /// 任务结束。ok 为 false 表示失败（取消不会发这个信号）。
    void finished(bool ok, Kind kind, const QString &targetPath);

private:
    void beginTransfer();
    void scheduleRetry();
    void onTransportProgress(qint64 received, qint64 total);
    void onTransportFinished(EngineFetchOutcome outcome);
    void finish(bool ok);
    void extractFfmpeg(const QString &zipPath);
    /// 收尾：清临时文件、复位状态；busy 归零后页面据此刷新按钮。
    void reset();

    QString kindLabel() const;

    IEngineTransport *m_transport = nullptr;
    IChildProcess *m_unzip = nullptr; ///< 解压 ffmpeg 那个 zip 用的进程
    bool m_ownsExternals = false;     ///< 真实实现才由本对象负责销毁

    Kind m_kind = Kind::None;
    QString m_url;
    QString m_partPath;
    QString m_targetPath;
    int m_attempt = 0;   ///< 已发起的传输次数，到上限就放弃
    qint64 m_offset = 0; ///< 本次续传的起始偏移（即已落盘的字节数）
    /// 是否有一个传输正在飞。取消要靠它分流：在飞就 abort（由 finished 收尾），
    /// 不在飞（正等着 1.5 秒后重试）就直接收尾 —— 少了这个标志，重试等待期间的
    /// 取消会被随后的定时器悄悄重新拉起来。
    bool m_transferActive = false;
    bool m_cancelled = false;
};
