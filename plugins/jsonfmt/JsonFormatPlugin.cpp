#include "JsonFormatPlugin.h"

#include "core/JsonFormat.h"

#include <QClipboard>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

/// 工具页面：JSON 格式化 / 压缩。
///
/// 它除了是界面，还实现了 toolbox::IToolPage —— 外壳会在页面创建后调用
/// restoreState()，在窗口关闭或重载插件前调用 saveState()，所以插件自己
/// 不用操心 QSettings 的键名，也不会和别的插件串键。
class JsonFormatPage : public QWidget, public toolbox::IToolPage
{
    Q_OBJECT
    Q_INTERFACES(toolbox::IToolPage)

public:
    explicit JsonFormatPage(QWidget *parent = nullptr);

    void restoreState(const toolbox::ToolSettings &settings) override;
    void saveState(const toolbox::ToolSettings &settings) override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 拖进来的文件最多读 1 MB。再大只会把界面卡住。
    static constexpr qint64 kMaxDropBytes = 1024 * 1024;

    void convert(bool compact);
    void readDroppedFile(const QString &path);

    QPlainTextEdit *m_source = nullptr;
    QPlainTextEdit *m_result = nullptr;
    QLabel *m_hint = nullptr;
    QComboBox *m_indent = nullptr;
    QPushButton *m_copy = nullptr;
};

JsonFormatPage::JsonFormatPage(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);

    m_source = new QPlainTextEdit(this);
    m_source->setPlaceholderText(tr("在这里粘贴 JSON —— 也可以把 .json 文件拖进来"));

    m_result = new QPlainTextEdit(this);
    m_result->setPlaceholderText(tr("结果"));
    m_result->setReadOnly(true);

    // 顺序与 jsonfmt::Indent 一致，项的 data 存枚举值 —— 存配置只存下标，
    // 万一将来插入新样式，靠 data 认而不是靠下标猜。
    m_indent = new QComboBox(this);
    m_indent->addItem(tr("2 空格"), static_cast<int>(jsonfmt::Indent::TwoSpaces));
    m_indent->addItem(tr("4 空格"), static_cast<int>(jsonfmt::Indent::FourSpaces));
    m_indent->addItem(tr("Tab"), static_cast<int>(jsonfmt::Indent::Tab));
    m_indent->setCurrentIndex(static_cast<int>(jsonfmt::Indent::FourSpaces));

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);

    m_copy = new QPushButton(tr("复制结果"), this);
    m_copy->setEnabled(false);

    auto *beautifyButton = new QPushButton(tr("格式化"), this);
    auto *compactButton = new QPushButton(tr("压缩"), this);
    auto *clearButton = new QPushButton(tr("清空"), this);

    // 快捷键写在 tooltip 里，否则没人知道它们存在。
    beautifyButton->setToolTip(tr("格式化（Ctrl+Enter）"));
    compactButton->setToolTip(tr("压缩（Ctrl+Shift+Enter）"));

    auto *options = new QHBoxLayout;
    options->addWidget(new QLabel(tr("缩进"), this));
    options->addWidget(m_indent);
    options->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(beautifyButton);
    buttons->addWidget(compactButton);
    buttons->addStretch(1);
    buttons->addWidget(m_copy);
    buttons->addWidget(clearButton);

    layout->addWidget(m_source, 1);
    layout->addLayout(options);
    layout->addLayout(buttons);
    layout->addWidget(m_hint);
    layout->addWidget(m_result, 1);

    connect(beautifyButton, &QPushButton::clicked, this, [this] { convert(false); });
    connect(compactButton, &QPushButton::clicked, this, [this] { convert(true); });

    // 大回车与小键盘回车各接一个，否则用另一块键盘的人会觉得快捷键时灵时不灵。
    for (const auto &keys : {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter))}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, beautifyButton, &QPushButton::click);
    }
    for (const auto &keys :
         {QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Return)),
          QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Enter))}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, compactButton, &QPushButton::click);
    }

    connect(m_copy, &QPushButton::clicked, this, [this] {
        const QString text = m_result->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        // 剪贴板没有视觉反馈，不说一句用户会怀疑有没有生效。
        m_hint->setText(tr("已复制结果到剪贴板。"));
    });

    // 复制按钮的可用状态跟着结果走，省得在每处 setPlainText 之后单独维护。
    connect(m_result, &QPlainTextEdit::textChanged, this,
            [this] { m_copy->setEnabled(!m_result->toPlainText().isEmpty()); });

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_source->clear();
        m_result->clear();
        m_hint->clear();
    });
}

void JsonFormatPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void JsonFormatPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    readDroppedFile(path);
    event->acceptProposedAction();
}

void JsonFormatPage::readDroppedFile(const QString &path)
{
    const QFileInfo info(path);

    if (info.size() > kMaxDropBytes) {
        m_hint->setText(tr("「%1」有 %2 KB，超过 1 MB —— 再大只会把界面卡住。")
                            .arg(info.fileName())
                            .arg(info.size() / 1024));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_hint->setText(tr("读不到「%1」：%2").arg(info.fileName(), file.errorString()));
        return;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.contains('\0')) {
        m_hint->setText(tr("「%1」不像文本文件（含二进制内容）。").arg(info.fileName()));
        return;
    }

    m_source->setPlainText(QString::fromUtf8(bytes));
    m_hint->setText(tr("已读入 %1（%2 字节）").arg(info.fileName()).arg(bytes.size()));
}

void JsonFormatPage::convert(bool compact)
{
    const QString source = m_source->toPlainText();
    const jsonfmt::Indent indent = static_cast<jsonfmt::Indent>(m_indent->currentData().toInt());
    const jsonfmt::FormatResult formatted = jsonfmt::formatJson(source, compact, indent);

    if (!formatted.ok) {
        m_result->clear();
        // 报行列而不是裸偏移：用户要去的是「第几行第几列」，偏移数没法和编辑器对上。
        const jsonfmt::ErrorLocation at = jsonfmt::locateError(source, formatted.errorOffset);
        if (at.line > 0) {
            m_hint->setText(tr("解析失败：第 %1 行第 %2 列 —— %3")
                                .arg(at.line)
                                .arg(at.column)
                                .arg(formatted.errorText));
        } else {
            m_hint->setText(
                tr("解析失败：偏移 %1 —— %2").arg(formatted.errorOffset).arg(formatted.errorText));
        }
        return;
    }

    m_hint->clear();
    m_result->setPlainText(formatted.text);
}

void JsonFormatPage::restoreState(const toolbox::ToolSettings &settings)
{
    const int index =
        settings.value(QStringLiteral("indent"), static_cast<int>(jsonfmt::Indent::FourSpaces))
            .toInt();
    if (index >= 0 && index < m_indent->count()) {
        m_indent->setCurrentIndex(index);
    }
}

void JsonFormatPage::saveState(const toolbox::ToolSettings &settings)
{
    settings.setValue(QStringLiteral("indent"), m_indent->currentIndex());
}

toolbox::ToolMeta JsonFormatPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("dev.json-format");
    info.name = tr("JSON 格式化");
    info.category = tr("开发辅助");
    info.version = QStringLiteral("0.2.0");
    info.description = tr("格式化、压缩 JSON，并提示语法错误位置。");
    info.icon = QIcon(QStringLiteral(":/icons/jsonfmt.svg"));
    return info;
}

QWidget *JsonFormatPlugin::createPage(QWidget *parent)
{
    return new JsonFormatPage(parent);
}

#include "JsonFormatPlugin.moc"
