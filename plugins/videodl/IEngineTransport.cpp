#include "IEngineTransport.h"

#include "core/OutputParsing.h"

#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace {

/// 单次传输的超时（毫秒）。没有它，连接被中间设备挂住时会永远卡在某个百分比上不报错。
constexpr int kTransferTimeoutMs = 30000;

} // namespace

NetworkEngineTransport::NetworkEngineTransport(QObject *parent)
    : IEngineTransport(parent)
    , m_net(new QNetworkAccessManager(this))
{}

NetworkEngineTransport::~NetworkEngineTransport()
{
    delete m_file;
    m_file = nullptr;
    if (m_reply) {
        m_reply->abort();
    }
}

void NetworkEngineTransport::fetch(const EngineFetchRequest &request)
{
    delete m_file;
    m_file = nullptr;

    m_file = new QFile(request.partPath, this);
    const QIODevice::OpenMode mode =
        request.offset > 0 ? (QIODevice::WriteOnly | QIODevice::Append) : QIODevice::WriteOnly;
    if (!m_file->open(mode)) {
        // 打不开就无法续传也无法落盘，直接报失败：EngineFetcher 会走 Finish 分支并
        // 把原因带给用户，比「传输到一半才发现写不进去」好排查。
        EngineFetchOutcome outcome;
        outcome.transportFailed = true;
        outcome.errorText = tr("无法写入临时文件");
        emit finished(outcome);
        return;
    }

    QNetworkRequest netRequest{request.url};
    netRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                            QNetworkRequest::NoLessSafeRedirectPolicy);
    // GitHub 的 release 资源会跳到 release-assets.githubusercontent.com，
    // Qt 默认走 HTTP/2 连那个主机时容易一个字节都收不到，退回 HTTP/1.1 更稳。
    netRequest.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    netRequest.setTransferTimeout(kTransferTimeoutMs);
    if (request.offset > 0) {
        netRequest.setRawHeader(
            "Range", "bytes=" + QByteArray::number(request.offset) + QByteArrayLiteral("-"));
    }

    QNetworkReply *reply = m_net->get(netRequest);
    m_reply = reply;

    connect(reply, &QNetworkReply::downloadProgress, this, &IEngineTransport::progress);
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (m_file) {
            m_file->write(reply->readAll());
        }
    });
    connect(reply, &QNetworkReply::finished, this, &NetworkEngineTransport::onReplyFinished);
}

void NetworkEngineTransport::abort()
{
    if (m_reply) {
        // abort() 会立刻触发 finished，收尾统一走 onReplyFinished。
        m_reply->abort();
    }
}

void NetworkEngineTransport::onReplyFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;

    delete m_file;
    m_file = nullptr;

    EngineFetchOutcome outcome;
    if (!reply) {
        outcome.transportFailed = true;
        outcome.errorText = tr("网络请求已取消");
        emit finished(outcome);
        return;
    }

    outcome.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    outcome.transportFailed = reply->error() != QNetworkReply::NoError;
    outcome.errorText = reply->errorString();
    if (!outcome.transportFailed && outcome.statusCode == 206) {
        outcome.contentRangeStart =
            videodl::parseContentRangeStart(QString::fromLatin1(reply->rawHeader("Content-Range")));
    }

    reply->deleteLater();
    emit finished(outcome);
}
