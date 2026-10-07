#include "WatermarkText.h"

#include <QCoreApplication>

const QVector<OutputFormat> &outputFormats()
{
    // 静态局部：只构造一次，且 tr() 在首次调用时求值（此时QApplication 已存在）。
    static const QVector<OutputFormat> formats = {
        OutputFormat(QCoreApplication::translate("imgwatermark", "PNG（无损，水印最可靠）"),
                     QStringLiteral("png"), false),
        OutputFormat(QCoreApplication::translate("imgwatermark", "JPEG（有损，体积小）"),
                     QStringLiteral("jpg"), true),
        OutputFormat(QCoreApplication::translate("imgwatermark", "WebP（有损，体积小）"),
                     QStringLiteral("webp"), true),
        OutputFormat(QCoreApplication::translate("imgwatermark", "BMP（无损，体积大）"),
                     QStringLiteral("bmp"), false),
    };
    return formats;
}

QString autoWatermarkText(const QString &baseName, const QSize &pixelSize, qint64 fileSize,
                          const QDateTime &modified)
{
    // 四要素缺一不可：少任何一项都会让「日后凭水印认出原图」这件事做不到 ——
    // 只有文件名时同名文件会混淆，只有时间时同一批导出无法区分。
    QString text = QStringLiteral("ToolBox · %1").arg(baseName);
    if (pixelSize.isValid()) {
        text += QStringLiteral(" · %1×%2").arg(pixelSize.width()).arg(pixelSize.height());
    }
    text += QStringLiteral(" · %1 B").arg(fileSize);
    if (modified.isValid()) {
        text += QStringLiteral(" · %1").arg(modified.toString(Qt::ISODate));
    }
    return text;
}
