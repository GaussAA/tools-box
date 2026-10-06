#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

#include <QtGlobal>

// QNetworkAccessManager / QNetworkReply / QFile 只在 .cpp 的实现里用到，头文件里
// 前向声明即可 —— 接口的使用者（包括测试里的假实现）不该被迫拉进这几个依赖。
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// 「把一个内核文件搬到磁盘」的可注入抽象。
//
// 抽它的理由与 IChildProcess 相同：EngineFetcher 里真正值得测的是**决策** ——
// 什么情况下续传、什么情况下整个重来、重试到几次放弃、用户取消与重试撞在一起时谁赢。
// 这些分支此前只能靠真机下载去撞（docs/workflow.md §8 把内核下载列为「必跑」正是
// 因为它们没有别的防线），而其中「取消之后重试定时器又悄悄把下载拉起来」这条，
// 撞上的概率完全取决于手速。
//
// 抽象之后测试可以直接喂一个 outcome（200 / 206 起点错位 / 网络错误 / 取消），
// 每条分支都能确定地走到，且**既不碰网络也不碰磁盘之外的任何外部系统**。
//
// 职责边界：字节落盘归传输器（假的那个什么都不写），「下一次该怎么要」归
// EngineFetcher —— 所以 offset 由调用方算好传进来，不由传输器自己看文件大小。

/// 一次传输的请求。
struct EngineFetchRequest
{
    QUrl url;
    QString partPath; ///< 落盘目标（.part 临时文件）
    /// 续传起点：>0 表示上次已经落盘了这么多字节，本次只要后面的部分。
    qint64 offset = 0;
};

/// 一次传输的结果。纯数据，便于测试直接构造。
struct EngineFetchOutcome
{
    /// 传输层是否失败（网络错误、或 reply 根本没建立）。取消也会置它 ——
    /// 取消与否由调用方自己的状态判断，本字段只描述「传输这次没成」。
    bool transportFailed = false;
    int statusCode = 0;
    QString errorText;
    /// 206 响应里 Content-Range 的起始字节；非 206 或解析不出时为 -1。
    qint64 contentRangeStart = -1;
};

class IEngineTransport : public QObject
{
    Q_OBJECT

public:
    explicit IEngineTransport(QObject *parent = nullptr)
        : QObject(parent)
    {}
    ~IEngineTransport() override = default;

    /// 开始一次传输。结果通过 finished 给出，不阻塞。
    virtual void fetch(const EngineFetchRequest &request) = 0;

    /// 中止当前传输。**中止后仍必须给出一次 finished** —— 调用方靠它收尾，
    /// 少发一次就会让状态机永远停在忙碌态。
    virtual void abort() = 0;

signals:
    /// received / total 只统计本次请求的范围（续传时不含已落盘的部分）。
    void progress(qint64 received, qint64 total);
    void finished(EngineFetchOutcome outcome);
};

/// 真的那个：QNetworkAccessManager + 落盘。
class NetworkEngineTransport : public IEngineTransport
{
public:
    explicit NetworkEngineTransport(QObject *parent = nullptr);
    ~NetworkEngineTransport() override;

    void fetch(const EngineFetchRequest &request) override;
    void abort() override;

private:
    void onReplyFinished();

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QFile *m_file = nullptr;
};
