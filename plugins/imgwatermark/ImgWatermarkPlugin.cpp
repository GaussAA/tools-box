#include "ImgWatermarkPlugin.h"

#include "WatermarkText.h"

#include "core/Stego.h"
#include "core/StegoPayload.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent>

namespace {

/// 单张图片的体积上限（MB）。
///
/// 上限存在的理由是嵌入是纯 CPU 运算，耗时与像素数成正比；一张 8000×6000 的
/// 图要跑几十秒，用户等不起也会以为程序卡死。设成上限并给出明确提示，比让它
/// 慢慢跑完更符合「稳定优先」的取舍。
constexpr qint64 kMaxFileSizeMB = 40;

/// 界面配色：把预览图的背景与文字显式定下来，避免在深色系统上出现黑底黑图。
const QString kPreviewBackdrop = QStringLiteral("#2b2b2b");
const QString kPreviewText = QStringLiteral("#d0d0d0");

} // namespace

/// 工具页面：图片数字水印。
///
/// 除了界面，它实现了 toolbox::IToolPage —— 强度、格式、质量等参数由外壳自动
/// 存取，插件不碰 QSettings 键名。
class ImgWatermarkPage : public QWidget, public toolbox::IToolPage
{
    Q_OBJECT
    Q_INTERFACES(toolbox::IToolPage)

public:
    explicit ImgWatermarkPage(QWidget *parent = nullptr);
    ~ImgWatermarkPage() override;

    void restoreState(const toolbox::ToolSettings &settings) override;
    void saveState(const toolbox::ToolSettings &settings) override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void buildUi();
    void pickImage();
    /// 把指定路径的图片读成当前原图。「选择图片…」与拖放共用这一条路径：
    /// 同样的格式校验、同样的大小上限、同样的报错措辞 —— 各写一份迟早会
    /// 长出两套行为。返回是否成功，失败时自己已经把原因告诉用户了。
    bool loadSourceFile(const QString &path);
    void startEmbed();
    void startExtract();
    void saveResult();
    void onEmbedded(const imgwatermark::EmbedResult &result);
    void onExtracted(const imgwatermark::ExtractResult &result);
    void setBusy(bool busy);
    void setStatus(const QString &text);
    /// 依据当前原图刷新容量提示与「能不能装得下」的判断。
    void refreshCapacityHint();
    void updateQualityEnabled();
    void clearAll();
    /// 当前选中的输出格式。
    const OutputFormat &currentFormat() const;
    /// 供后台线程回调的载荷内容（UTF-8 文本）。
    QByteArray buildPayloadFromUi() const;

    // 输入区
    QLabel *m_fileLabel = nullptr;
    QPushButton *m_pickButton = nullptr;

    // 预览区
    QLabel *m_beforeLabel = nullptr;
    QLabel *m_afterLabel = nullptr;

    // 参数区
    QRadioButton *m_customText = nullptr;
    QRadioButton *m_autoText = nullptr;
    QLineEdit *m_textEdit = nullptr;
    QSlider *m_strength = nullptr;
    QSpinBox *m_stripes = nullptr;
    QComboBox *m_format = nullptr;
    QSlider *m_quality = nullptr;
    QLabel *m_qualityLabel = nullptr;

    // 操作区
    QPushButton *m_embedButton = nullptr;
    QPushButton *m_extractButton = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_status = nullptr;

    QImage m_source;
    QImage m_result;
    QString m_sourcePath;
    QWidget *m_paramBox = nullptr;
};

ImgWatermarkPage::ImgWatermarkPage(QWidget *parent)
    : QWidget(parent)
{
    // 这个工具最顺手的用法就是把图片直接拖进来，不必先点「选择图片…」。
    setAcceptDrops(true);
    buildUi();
}

ImgWatermarkPage::~ImgWatermarkPage() = default;

void ImgWatermarkPage::buildUi()
{
    auto *root = new QVBoxLayout(this);

    // ── 输入区 ──────────────────────────────────────────────────────────────
    auto *inputRow = new QHBoxLayout;
    m_fileLabel = new QLabel(tr("尚未选择图片"), this);
    m_pickButton = new QPushButton(tr("选择图片…"), this);
    inputRow->addWidget(m_fileLabel, 1);
    inputRow->addWidget(m_pickButton);

    // ── 预览区（左右对照：水印不可见的直观证据）─────────────────────────────
    auto *previewRow = new QHBoxLayout;
    const auto makePreview = [this, previewRow](const QString &title) {
        auto *box = new QGroupBox(title, this);
        auto *layout = new QVBoxLayout(box);
        auto *label = new QLabel(box);
        label->setAlignment(Qt::AlignCenter);
        label->setMinimumHeight(220);
        label->setStyleSheet(QStringLiteral("background:%1; color:%2; border-radius:4px;")
                                 .arg(kPreviewBackdrop, kPreviewText));
        layout->addWidget(label, 1);
        previewRow->addWidget(box, 1);
        return label;
    };
    m_beforeLabel = makePreview(tr("原图"));
    m_afterLabel = makePreview(tr("处理后（肉眼看不出差别）"));

    // ── 参数区 ──────────────────────────────────────────────────────────────
    m_paramBox = new QWidget(this);
    auto *paramForm = new QFormLayout(m_paramBox);

    auto *textRow = new QHBoxLayout;
    m_customText = new QRadioButton(tr("自定义文本"), m_paramBox);
    m_autoText = new QRadioButton(tr("按文件信息自动生成"), m_paramBox);
    m_customText->setChecked(true);
    m_textEdit = new QLineEdit(m_paramBox);
    m_textEdit->setPlaceholderText(tr("例如：© 2026某某科技 · 仅供内部使用"));
    m_textEdit->setMaximumWidth(340);
    textRow->addWidget(m_customText);
    textRow->addWidget(m_textEdit, 1);
    textRow->addWidget(m_autoText);
    paramForm->addRow(tr("水印内容"), textRow);

    auto *strengthRow = new QHBoxLayout;
    m_strength = new QSlider(Qt::Horizontal, m_paramBox);
    m_strength->setRange(static_cast<int>(imgwatermark::kMinStrength),
                         static_cast<int>(imgwatermark::kMaxStrength));
    m_strength->setValue(static_cast<int>(imgwatermark::kDefaultStrength));
    m_strength->setMaximumWidth(260);
    auto *strengthValue = new QLabel(m_paramBox);
    strengthValue->setMinimumWidth(60);
    strengthRow->addWidget(m_strength, 1);
    strengthRow->addWidget(strengthValue);
    paramForm->addRow(tr("嵌入强度"), strengthRow);

    auto *stripeRow = new QHBoxLayout;
    m_stripes = new QSpinBox(m_paramBox);
    m_stripes->setRange(1, imgwatermark::kMaxStripes);
    m_stripes->setValue(1);
    m_stripes->setMaximumWidth(90);
    stripeRow->addWidget(m_stripes);
    stripeRow->addWidget(new QLabel(tr("条冗余带，抗裁剪更强但单带容量更小"), m_paramBox), 1);
    paramForm->addRow(tr("冗余"), stripeRow);

    auto *formatRow = new QHBoxLayout;
    m_format = new QComboBox(m_paramBox);
    const auto &formats = outputFormats();
    for (const OutputFormat &format : formats) {
        m_format->addItem(format.label(), format.suffix());
    }
    m_format->setMaximumWidth(280);
    m_quality = new QSlider(Qt::Horizontal, m_paramBox);
    m_quality->setRange(40, 100);
    m_quality->setValue(92);
    m_quality->setMaximumWidth(160);
    m_qualityLabel = new QLabel(QStringLiteral("92"), m_paramBox);
    formatRow->addWidget(m_format);
    formatRow->addWidget(m_quality);
    formatRow->addWidget(m_qualityLabel);
    paramForm->addRow(tr("输出格式与质量"), formatRow);

    // ── 操作区 ──────────────────────────────────────────────────────────────
    auto *actionBox = new QWidget(this);
    auto *actionRow = new QHBoxLayout(actionBox);
    m_embedButton = new QPushButton(tr("嵌入水印"), actionBox);
    m_extractButton = new QPushButton(tr("提取水印"), actionBox);
    m_saveButton = new QPushButton(tr("另存为…"), actionBox);
    m_clearButton = new QPushButton(tr("清空"), actionBox);
    m_saveButton->setEnabled(false);
    actionRow->addWidget(m_embedButton);
    actionRow->addWidget(m_extractButton);
    actionRow->addWidget(m_saveButton);
    actionRow->addStretch(1);
    actionRow->addWidget(m_clearButton);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);

    root->addLayout(inputRow);
    root->addLayout(previewRow, 1);
    root->addWidget(m_paramBox);
    root->addWidget(actionBox);
    root->addWidget(m_progress);
    root->addWidget(m_status);

    // ── 信号 ────────────────────────────────────────────────────────────────
    connect(m_pickButton, &QPushButton::clicked, this, &ImgWatermarkPage::pickImage);
    connect(m_embedButton, &QPushButton::clicked, this, &ImgWatermarkPage::startEmbed);
    connect(m_extractButton, &QPushButton::clicked, this, &ImgWatermarkPage::startExtract);
    connect(m_saveButton, &QPushButton::clicked, this, &ImgWatermarkPage::saveResult);
    connect(m_clearButton, &QPushButton::clicked, this, &ImgWatermarkPage::clearAll);
    connect(m_format, &QComboBox::currentIndexChanged, this,
            &ImgWatermarkPage::updateQualityEnabled);
    connect(m_customText, &QRadioButton::toggled, m_textEdit, &QLineEdit::setEnabled);
    connect(m_strength, &QSlider::valueChanged, strengthValue,
            [strengthValue](int value) { strengthValue->setText(QString::number(value)); });
    connect(m_quality, &QSlider::valueChanged, this,
            [this](int value) { m_qualityLabel->setText(QString::number(value)); });

    updateQualityEnabled();
    refreshCapacityHint();
}

const OutputFormat &ImgWatermarkPage::currentFormat() const
{
    const auto &formats = outputFormats();
    const int index = m_format->currentIndex();
    return formats.at(index < 0 ? 0 : index);
}

void ImgWatermarkPage::updateQualityEnabled()
{
    const bool supports = currentFormat().supportsQuality();
    m_quality->setEnabled(supports);
    m_qualityLabel->setEnabled(supports);
}

void ImgWatermarkPage::pickImage()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("选择图片"), QString(),
        tr("图片文件 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"));
    if (path.isEmpty()) {
        return;
    }
    loadSourceFile(path);
}

void ImgWatermarkPage::dragEnterEvent(QDragEnterEvent *event)
{
    // 只接文件：往这里拖一段文字没有意义。
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void ImgWatermarkPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    loadSourceFile(path);
    event->acceptProposedAction();
}

bool ImgWatermarkPage::loadSourceFile(const QString &path)
{
    const QFileInfo info(path);

    // 格式先判：让用户拿到「这不对」比让他等几秒再看到同样的话要好。
    const QString suffix = info.suffix().toLower();
    const QVector<QString> supported = {"png", "jpg", "jpeg", "bmp", "webp"};
    if (!supported.contains(suffix)) {
        QMessageBox::warning(this, tr("不支持的格式"),
                             tr("「%1」不是支持的图片格式。\n\n仅支持：PNG、JPEG、BMP、WebP。")
                                 .arg(info.fileName()));
        return false;
    }

    if (info.size() > kMaxFileSizeMB * 1024 * 1024) {
        QMessageBox::warning(this, tr("文件过大"),
                             tr("图片大小 %1 MB，超出本工具 %2 MB 的处理上限。\n\n"
                                "水印嵌入需要逐像素运算，过大的图片处理时间过长。")
                                 .arg(info.size() / (1024 * 1024))
                                 .arg(kMaxFileSizeMB));
        return false;
    }

    QImage loaded(path);
    if (loaded.isNull()) {
        QMessageBox::warning(
            this, tr("无法读取图片"),
            tr("读取「%1」失败。文件可能已损坏，或不是有效的图片。").arg(info.fileName()));
        return false;
    }

    m_source = loaded;
    m_sourcePath = path;
    m_result = QImage();
    m_fileLabel->setText(tr("%1 · %2×%3 · %4 KB")
                             .arg(info.fileName())
                             .arg(m_source.width())
                             .arg(m_source.height())
                             .arg(info.size() / 1024));
    m_beforeLabel->setPixmap(QPixmap::fromImage(m_source).scaled(
        m_beforeLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_afterLabel->clear();
    m_afterLabel->setText(tr("尚未处理"));
    m_saveButton->setEnabled(false);
    m_textEdit->setEnabled(m_customText->isChecked());
    refreshCapacityHint();
    setStatus(tr("已载入 %1×%2，可嵌入水印").arg(m_source.width()).arg(m_source.height()));
    return true;
}

void ImgWatermarkPage::refreshCapacityHint()
{
    if (m_status->text().isEmpty()) {
        return;
    }
    if (m_source.isNull()) {
        return;
    }
    const int capacity = imgwatermark::capacityBytes(m_source.size());
    const QByteArray payload = buildPayloadFromUi();
    const int need = payload.size();
    if (need > capacity) {
        setStatus(tr("⚠ 当前图片最多可嵌入 %1 字节，水印内容需 %2 字节 —— 请缩短内容或换更大的图")
                      .arg(capacity)
                      .arg(need));
    }
}

QByteArray ImgWatermarkPage::buildPayloadFromUi() const
{
    QString text = m_customText->isChecked() ? m_textEdit->text() : QString();
    if (!m_customText->isChecked()) {
        const QFileInfo info(m_sourcePath);
        text = autoWatermarkText(info.completeBaseName(), m_source.size(), info.size(),
                                 info.lastModified());
    }
    if (text.trimmed().isEmpty()) {
        return QByteArray();
    }
    // 载荷 = 头 + 正文。头含 2 字节长度字段，所以正文上限要减去头长。
    const QByteArray raw =
        text.toUtf8().left(imgwatermark::kMaxPayloadBytes - imgwatermark::kPayloadHeaderBytes);
    return imgwatermark::buildPayload(raw);
}

void ImgWatermarkPage::startEmbed()
{
    if (m_source.isNull()) {
        QMessageBox::information(this, tr("未选择图片"), tr("请先选择一张图片。"));
        return;
    }
    const QByteArray payload = buildPayloadFromUi();
    if (payload.isEmpty()) {
        QMessageBox::information(this, tr("水印内容为空"),
                                 tr("请填写水印文本，或选择「按文件信息自动生成」。"));
        return;
    }

    setBusy(true);
    setStatus(tr("正在嵌入水印…"));

    // 后台线程执行：嵌入是纯 CPU 运算，一张大图要几秒，放 GUI 线程会卡住界面
    // （见 docs/architecture.md §5）。进度回调同样在工作线程上被调用，
    // 所以用 QueuedConnection 切回界面线程。
    const QImage source = m_source;
    const double strength = m_strength->value();
    const int stripes = m_stripes->value();
    m_progress->setValue(0);
    m_progress->setVisible(true);

    // 进度回调是在**工作线程**上被调用的，不能直接动界面控件 —— 必须用
    // QueuedConnection 切回 GUI 线程。QProgressBar 不是线程安全的，
    // 直接 setValue 在某些配置下会稳定复现崩溃。
    QProgressBar *progressBar = m_progress;
    auto reportProgress = [progressBar](int percent) {
        QMetaObject::invokeMethod(
            progressBar, [progressBar, percent]() { progressBar->setValue(percent); },
            Qt::QueuedConnection);
    };

    auto *watcher = new QFutureWatcher<imgwatermark::EmbedResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        const imgwatermark::EmbedResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        m_progress->setVisible(false);
        onEmbedded(result);
    });

    watcher->setFuture(QtConcurrent::run([source, payload, strength, stripes, reportProgress]() {
        return imgwatermark::embedWatermark(source, payload, strength, stripes, reportProgress);
    }));
}

void ImgWatermarkPage::startExtract()
{
    if (m_source.isNull()) {
        QMessageBox::information(this, tr("未选择图片"),
                                 tr("请先选择一张图片 —— 提取水印需要读取原图或处理后的图。"));
        return;
    }

    setBusy(true);
    m_progress->setValue(0);
    m_progress->setVisible(true);
    setStatus(tr("正在提取水印…"));

    const QImage source = m_source;
    auto *watcher = new QFutureWatcher<imgwatermark::ExtractResult>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        const imgwatermark::ExtractResult result = watcher->result();
        watcher->deleteLater();
        setBusy(false);
        m_progress->setVisible(false);
        onExtracted(result);
    });
    watcher->setFuture(
        QtConcurrent::run([source]() { return imgwatermark::extractWatermark(source); }));
}

void ImgWatermarkPage::onEmbedded(const imgwatermark::EmbedResult &result)
{
    if (!result.ok) {
        QMessageBox::warning(this, tr("嵌入失败"), result.errorText);
        setStatus(tr("✗ 嵌入失败：%1").arg(result.errorText));
        return;
    }

    m_result = result.image;
    m_afterLabel->setPixmap(QPixmap::fromImage(m_result).scaled(
        m_afterLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_saveButton->setEnabled(true);
    setStatus(tr("✓ 已嵌入：%1 条冗余带 · %2 块 · 载荷 %3 字节（容量 %4 字节）")
                  .arg(result.stripes)
                  .arg(result.blocks)
                  .arg(result.usedBytes)
                  .arg(result.capacityBytes));
}

void ImgWatermarkPage::onExtracted(const imgwatermark::ExtractResult &result)
{
    if (!result.ok) {
        QMessageBox::information(this, tr("未检测到水印"), result.errorText);
        setStatus(tr("✗ %1").arg(result.errorText));
        return;
    }
    const QString text = QString::fromUtf8(result.payload);

    // 提取出来的水印多半要拿去比对或登记，直接放进剪贴板省掉一次手选 ——
    // QMessageBox 里的文字是选不中的，不说一句用户也不知道已经能粘了。
    QGuiApplication::clipboard()->setText(text);

    setStatus(tr("✓ 提取成功（第 %1 条带校验通过）：%2 —— 已复制到剪贴板")
                  .arg(result.recoveredStripes)
                  .arg(text));
    QMessageBox::information(this, tr("提取成功"),
                             tr("水印内容：\n\n%1\n\n（已复制到剪贴板）\n\n"
                                "提取到了肉眼看不见的数据 —— 这正是数字水印与可见水印的区别。")
                                 .arg(text));
}

void ImgWatermarkPage::saveResult()
{
    if (m_result.isNull()) {
        return;
    }
    const OutputFormat &format = currentFormat();
    const QString suggestion =
        QFileInfo(m_sourcePath).completeBaseName() + QStringLiteral("_watermark.");
    const QString path = QFileDialog::getSaveFileName(this, tr("另存为"), suggestion);
    if (path.isEmpty()) {
        return;
    }

    // 没写扩展名就补上：用户手输「out」时 QImage::save 会因认不出格式而静默失败。
    QString target = path;
    if (QFileInfo(target).suffix().isEmpty()) {
        target += QLatin1Char('.') + format.suffix();
    }

    if (!m_result.save(target)) {
        QMessageBox::warning(
            this, tr("保存失败"),
            tr("写入「%1」失败。请检查目标目录是否有写入权限，以及磁盘空间是否充足。")
                .arg(QFileInfo(target).fileName()));
        setStatus(tr("✗ 保存失败：%1").arg(target));
        return;
    }

    setStatus(tr("✓ 已保存：%1").arg(target));
    QMessageBox::information(
        this, tr("已保存"),
        tr("水印已写入「%1」。\n\n提示：JPEG 是有损格式，水印强度不足时转存一次就可能"
           "提取不到；需要长期保真请用 PNG。")
            .arg(QFileInfo(target).fileName()));
}

void ImgWatermarkPage::clearAll()
{
    m_source = QImage();
    m_result = QImage();
    m_sourcePath.clear();
    m_fileLabel->setText(tr("尚未选择图片"));
    m_beforeLabel->clear();
    m_beforeLabel->setText(tr("尚未选择图片"));
    m_afterLabel->clear();
    m_afterLabel->setText(tr("尚未处理"));
    m_saveButton->setEnabled(false);
    setStatus(tr("已清空"));
}

void ImgWatermarkPage::setBusy(bool busy)
{
    // 显式逐个禁用，而不是遍历 findChildren 按名字排除 —— 后者要靠 objectName
    // 约定才能工作，加个新控件忘了改名就静默失效，属于典型的假绿。
    m_progress->setVisible(busy);
    m_pickButton->setEnabled(!busy);

    // 参数区整体禁掉：运算途中改参数没有意义（结果已按旧参数算好）。
    m_paramBox->setEnabled(!busy);

    m_embedButton->setEnabled(!busy && !m_source.isNull());
    m_extractButton->setEnabled(!busy && !m_source.isNull());
    m_saveButton->setEnabled(!busy && !m_result.isNull());
    m_clearButton->setEnabled(!busy);
}

void ImgWatermarkPage::setStatus(const QString &text)
{
    m_status->setText(text);
}

void ImgWatermarkPage::restoreState(const toolbox::ToolSettings &settings)
{
    m_strength->setValue(
        settings.value(QStringLiteral("strength"), static_cast<int>(imgwatermark::kDefaultStrength))
            .toInt());
    m_stripes->setValue(settings.value(QStringLiteral("stripes"), 1).toInt());
    m_quality->setValue(settings.value(QStringLiteral("quality"), 92).toInt());
    const QString format =
        settings.value(QStringLiteral("format"), QStringLiteral("png")).toString();
    const int index = m_format->findData(format);
    if (index >= 0) {
        m_format->setCurrentIndex(index);
    }
    m_customText->setChecked(settings.value(QStringLiteral("customText"), true).toBool());
    m_autoText->setChecked(!m_customText->isChecked());
    m_textEdit->setText(settings.value(QStringLiteral("text")).toString());
    m_textEdit->setEnabled(m_customText->isChecked());
}

void ImgWatermarkPage::saveState(const toolbox::ToolSettings &settings)
{
    settings.setValue(QStringLiteral("strength"), m_strength->value());
    settings.setValue(QStringLiteral("stripes"), m_stripes->value());
    settings.setValue(QStringLiteral("quality"), m_quality->value());
    settings.setValue(QStringLiteral("format"), currentFormat().suffix());
    settings.setValue(QStringLiteral("customText"), m_customText->isChecked());
    settings.setValue(QStringLiteral("text"), m_textEdit->text());
}

// ── 插件入口 ──────────────────────────────────────────────────

toolbox::ToolMeta ImgWatermarkPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("media.image-watermark");
    info.name = tr("图片数字水印");
    info.category = tr("媒体工具");
    info.version = QStringLiteral("0.1.0");
    info.description =
        tr("在图片像素中嵌入肉眼不可见的数字水印，用于版权溯源与内容验证（可再提取回来）。");
    info.icon = QIcon(QStringLiteral(":/icons/imgwatermark.svg"));
    return info;
}

QWidget *ImgWatermarkPlugin::createPage(QWidget *parent)
{
    return new ImgWatermarkPage(parent);
}

#include "ImgWatermarkPlugin.moc"
