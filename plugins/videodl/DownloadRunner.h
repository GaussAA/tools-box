#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

// 视频下载插件里「跑 yt-dlp 并把它的输出翻译成界面能用的信号」的那部分。
//
// 它原先是页面上的 m_process 加一串槽（见 docs/architecture.md 计划 D2 的
// DownloadService）。它仍然持有 QProcess（所以不能进 core/），但**不认识任何控件** ——
// 与页面之间只靠信号说话：一行日志、一个百分比、一句阶段说明、一次结束。
//
// 输出行的解析规则本身在 core/OutputParsing，不含界面依赖，可单测。
class DownloadRunner : public QObject
{
    Q_OBJECT

public:
    explicit DownloadRunner(QObject *parent = nullptr);
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

    QProcess *m_process = nullptr;
    QByteArray m_pending; ///< 尚未凑成整行的程序输出
    QString m_outputPath; ///< 最近一次识别到的最终产物路径
};
