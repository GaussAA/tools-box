#pragma once

#include "IChildProcess.h"
#include "core/LineSplitter.h"

#include <QObject>
#include <QString>
#include <QStringList>

// 视频下载插件里「跑 yt-dlp 并把它的输出翻译成界面能用的信号」的那部分。
//
// 它原先是页面上的 m_process 加一串槽（见 docs/architecture.md 计划 D2 的
// DownloadService）。它**不认识任何控件**，与页面之间只靠信号说话：一行日志、一个
// 百分比、一句阶段说明、一次结束。
//
// 输出行的解析规则本身在 core/OutputParsing，不含界面依赖，可单测。
//
// 为什么它不进 `core/`：不是因为用了 QProcess（那是 Qt Core 的类），而是因为它
// 持有**用户可见的 `tr()` 文案** —— 按 docs/architecture.md §3 的判定表，把 Model
// 的变化刷成界面文案属于 View 这一侧。
//
// 子进程经 IChildProcess 注入：构造时不传就自己造一个真的；测试传一个假的进来，
// 于是不用真跑 yt-dlp 也能验证「哪一行输出对应哪个阶段」。
class DownloadRunner : public QObject
{
    Q_OBJECT

public:
    /// 生产用法：自己持有一个真实的子进程。
    explicit DownloadRunner(QObject *parent = nullptr);

    /// 测试用法：用注入的子进程（可以是假的）。**所有权不转移**，调用方负责它的
    /// 生命周期，且必须活到本对象销毁之后 —— 注入是为了替换行为，不是为了转移责任。
    explicit DownloadRunner(IChildProcess *process, QObject *parent = nullptr);

    ~DownloadRunner() override;

    bool isRunning() const;

    /// 启动一次下载。args 由 videodl::buildYtDlpArgs() 构造（core/，可单测）。
    void start(const QString &ytDlp, const QStringList &args);

    /// 取消。取消与失败在 finished 里由 crashed 区分，页面据此给出不同文案。
    void cancel();

signals:
    void logLine(const QString &line);
    /// 0–100；负数表示「没有百分比可报」，页面据此切到不确定态。
    void progress(int percent);
    void status(const QString &text);
    /// exitCode 为 0 且 crashed 为 false 才算成功；outputPath 可能为空（解析不出来时）。
    void finished(int exitCode, bool crashed, const QString &outputPath);

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    void handleLine(const QString &line);

    IChildProcess *m_process = nullptr;  ///< 可能是真的，也可能是测试注入的
    bool m_ownsProcess = false;          ///< 真的那个才由本对象负责销毁
    videodl::LineSplitter m_splitter;    ///< 跨块凑行：\r\n 跨块与末行无换行都在它那里处理
    QString m_outputPath;                ///< 最近一次识别到的最终产物路径
};
