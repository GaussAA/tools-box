#include "ToolIcons.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPixmap>
#include <QRectF>

namespace toolbox {

QIcon fallbackToolIcon(const QString &name)
{
    constexpr int kSize = 48;

    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor::fromHsv(static_cast<int>(qHash(name) % 360u), 140, 200));
    painter.drawRoundedRect(QRectF(2, 2, kSize - 4, kSize - 4), 12, 12);

    QFont font = painter.font();
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter,
                     name.isEmpty() ? QStringLiteral("?") : name.left(1).toUpper());

    return QIcon(pixmap);
}

} // namespace toolbox
