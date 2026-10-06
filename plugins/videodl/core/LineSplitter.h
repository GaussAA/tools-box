#pragma once

#include <QByteArray>
#include <QList>

// 视频下载插件里「把子进程输出流按行切开」的纯逻辑。
//
// 之所以值得单独抽出来：yt-dlp 的输出是**按块**到达的，一行可能被切成两块
// （甚至 \r 和 \n 分在两块里），所以「收到 \n 就切」这种写法必然出错——
// 少了缓冲会把半行当整行送去解析，忘了 \r 会让每行尾巴多一个字符，
// 忘了收尾 flush 则永远丢掉最后一行。原先这段在 DownloadRunner::onReadyRead
// 里，只能靠真下载时「偶尔解析错一行」这种偶发症状暴露。
//
// 刻意停在字节层：解码与剥 ANSI 已在 core/OutputParsing（各有其测试），
// 这里只管「哪些字节算一行」，两个职责不混在一起。
//
// 本文件不得 include 任何 QtWidgets 头文件，也不得引用 QProcess。

namespace videodl {

/// 按行切分的缓冲器。一次 download 全程复用一个实例。
class LineSplitter
{
public:
    /// 追加一段刚收到的字节，返回本次**已经完整**的行（行尾 \r 已去掉）。
    ///
    /// 末尾没换行的半行会留在缓冲区里，等下一次 append 或 flush。
    QList<QByteArray> append(QByteArray chunk);

    /// 收尾：把缓冲区里剩下的内容作为最后一行返回（子进程退出时最后一行常无换行）。
    /// 返回后缓冲区为空，可继续复用。
    QByteArray flush();

private:
    QByteArray m_pending;
};

} // namespace videodl
