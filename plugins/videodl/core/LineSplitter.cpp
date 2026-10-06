#include "core/LineSplitter.h"

namespace videodl {

QList<QByteArray> LineSplitter::append(QByteArray chunk)
{
    m_pending += chunk;

    QList<QByteArray> lines;
    int newline = -1;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        QByteArray line = m_pending.left(newline);
        m_pending.remove(0, newline + 1);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        lines.append(line);
    }
    return lines;
}

QByteArray LineSplitter::flush()
{
    const QByteArray tail = m_pending;
    m_pending.clear();
    // 缓冲区里若只剩一个 \r（块尾的 \r 恰在这里结束），它不是内容。
    if (tail.endsWith('\r')) {
        return tail.left(tail.size() - 1);
    }
    return tail;
}

} // namespace videodl
