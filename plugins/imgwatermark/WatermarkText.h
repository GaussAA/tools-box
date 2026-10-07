#pragma once

#include <QDateTime>
#include <QImage>
#include <QString>
#include <QVector>

// 图片数字水印插件的「界面侧纯逻辑」：输出格式表与自动水印文本生成。
//
// 从 ImgWatermarkPlugin.cpp 拆出来的原因有两条，缺一不可：
//① 那个文件把界面装配（约 140 行 buildUi）、异步任务编排与这些小helper 混在一起，
//    已顶到 600 行的门禁（见 scripts/verify/verify_filesize.ps1）；
// ② **更重要的**：自动水印文本是「文件名 + 像素尺寸 + 体积 + 导出时间」的拼接，
//    它是本工具唯一的溯源信息生成逻辑，此前**无法单测**。拆出来后由
//    tests/tst_stego.cpp 的 autoWatermarkTextCarriesTraceableFields() 钉住 ——
//    「缺了哪一项都算退化」这条要求，从此有人守。

/// 输出格式选项：显示名 + 文件扩展名 + 是否支持质量滑块。
///
/// 做成**类**而非裸 struct：clang-tidy 的 `MemberPrefix` 规则要求类成员带 `m_`
/// 前缀（见 .clang-tidy），裸 struct 在本工程的调用方式下同样会被判为类成员而报错。
/// 用构造函数初始化而不是聚合初始化，正是因为成员名带了前缀、已非聚合体。
class OutputFormat
{
public:
    OutputFormat(QString label, QString suffix, bool supportsQuality)
        : m_label(std::move(label))
        , m_suffix(std::move(suffix))
        , m_supportsQuality(supportsQuality)
    {}

    QString label() const { return m_label; }
    QString suffix() const { return m_suffix; }
    bool supportsQuality() const { return m_supportsQuality; }

private:
    QString m_label;
    QString m_suffix;
    bool m_supportsQuality = false;
};

/// 支持的输出格式。
///
/// PNG 无损，是水印最可靠的容器；JPEG 有体积优势但每次保存都会损耗水印；
/// WebP 同样有损；BMP 无损但体积大。
const QVector<OutputFormat> &outputFormats();

/// 按文件信息生成一段可读的水印内容。
///
/// 用途是「懒得打字，但想让这张图带上自己的标识」：文件名 + 像素尺寸 + 体积 +
/// 导出时间，天然唯一。日后拿到这张图，一眼能认出是哪张原图、什么时候导出的 ——
/// 这正是溯源要的信息。`pixelSize` 无效时省略尺寸那项。
QString autoWatermarkText(const QString &baseName, const QSize &pixelSize, qint64 fileSize,
                          const QDateTime &modified);
