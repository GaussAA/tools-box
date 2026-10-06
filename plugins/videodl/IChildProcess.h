#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

// 「跑一个外部程序」的可注入抽象。
//
// 为什么需要它：DownloadRunner / DouyinResolver / EngineFetcher 这三个编排类的价值
// 全在「把外部世界的事件翻译成界面信号」——哪一行输出对应哪个阶段、超时与失败怎么
// 区分、取消与重试撞在一起时谁赢。这些规则此前**没有任何自动化覆盖**，唯一的防线是
// 周六那几个真机脚本（见 docs/workflow.md §8）。
//
// 真正卡住测试的并不是「不许起子进程」这条约定本身，而是这几个类把 QProcess 直接
// new 在构造函数里：想测它们就必须真的把 yt-dlp 或浏览器跑起来。加一层窄接口之后，
// 测试可以用一个假进程喂进预置的输出与退出码，**测的仍然是编排决策** —— 既不碰网络
// 也不碰子进程，约定没有被违反，只是不再能拿来当作不写测试的理由。
//
// 接口刻意只暴露这三个编排类真正用到的部分（起、杀、读、取错误串），不做成 QProcess
// 的完整替身：接口越窄，假实现越不容易写错，也越不容易长成第二个 QProcess。
class IChildProcess : public QObject
{
    Q_OBJECT

public:
    /// 通道合并方式。Merged 把 stderr 并进 stdout（yt-dlp 的进度与报错两边都有，
    /// 分开收会漏行）；Separate 分开收（渲染要单独拿到整份 DOM）。
    enum class ChannelMode { Separate, Merged };

    explicit IChildProcess(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~IChildProcess() override = default;

    virtual void setChannelMode(ChannelMode mode) = 0;

    /// 子进程环境。调用方负责把「要塞进去的变量」准备好（如 PYTHONUTF8）。
    virtual void setEnvironment(const QProcessEnvironment &env) = 0;

    virtual void start(const QString &program, const QStringList &args) = 0;
    virtual void kill() = 0;
    virtual bool isRunning() const = 0;

    virtual QByteArray readAllStandardOutput() = 0;
    virtual QByteArray readAllStandardError() = 0;

    /// 最后一次失败的原因，原样带给用户 —— 「无法启动」这种提示等于什么都没说。
    /// 仅在 errorOccurred 之后有意义。
    virtual QString errorString() const = 0;

signals:
    void readyReadStandardOutput();
    void finished(int exitCode, QProcess::ExitStatus status);
    void errorOccurred(QProcess::ProcessError error);
};

/// 真的那个：内部持有一个 QProcess，把它的信号原样转出来。
///
/// 析构时收尾进程（kill + 有限等待）也在这里完成 —— 原先这段逻辑散在三个编排类的
/// 析构函数里各写一遍，而漏写的后果是 "QProcess: Destroyed while process is still
/// running"。放在实现里，三个调用方都自动获得这个行为。
class RealChildProcess : public IChildProcess
{
public:
    explicit RealChildProcess(QObject *parent = nullptr);
    ~RealChildProcess() override;

    void setChannelMode(ChannelMode mode) override;
    void setEnvironment(const QProcessEnvironment &env) override;
    void start(const QString &program, const QStringList &args) override;
    void kill() override;
    bool isRunning() const override;
    QByteArray readAllStandardOutput() override;
    QByteArray readAllStandardError() override;
    QString errorString() const override;

private:
    QProcess *m_process = nullptr;
};
