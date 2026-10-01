#include "Base64Plugin.h"

#include <QByteArray>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

/// 工具页面：UTF-8 文本与 Base64 互转。
///
/// 它除了是界面，还实现了 toolbox::IToolPage —— 外壳会在页面创建后调用
/// restoreState()，在窗口关闭或重载插件前调用 saveState()，所以插件自己
/// 不用操心 QSettings 的键名，也不会和别的插件串键。
class Base64Page : public QWidget, public toolbox::IToolPage
{
    Q_OBJECT
    Q_INTERFACES(toolbox::IToolPage)

public:
    explicit Base64Page(QWidget *parent = nullptr);

    void restoreState(const toolbox::ToolSettings &settings) override;
    void saveState(const toolbox::ToolSettings &settings) override;

private:
    /// 按复选框决定用标准字符集还是 URL 安全字符集。
    QByteArray::Base64Options currentOptions() const;

    QPlainTextEdit *m_source = nullptr;
    QPlainTextEdit *m_result = nullptr;
    QCheckBox *m_urlSafe = nullptr;
};

Base64Page::Base64Page(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);

    m_source = new QPlainTextEdit(this);
    m_source->setPlaceholderText(tr("在这里输入原文，或粘贴 Base64"));

    m_result = new QPlainTextEdit(this);
    m_result->setPlaceholderText(tr("结果"));
    m_result->setReadOnly(true);

    auto *encodeButton = new QPushButton(tr("编码 →"), this);
    auto *decodeButton = new QPushButton(tr("← 解码"), this);
    auto *swapButton = new QPushButton(tr("结果转原文"), this);
    auto *clearButton = new QPushButton(tr("清空"), this);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(encodeButton);
    buttons->addWidget(decodeButton);
    buttons->addStretch(1);
    buttons->addWidget(swapButton);
    buttons->addWidget(clearButton);

    m_urlSafe = new QCheckBox(tr("URL 安全字符集（用 - _ 代替 + /）"), this);

    layout->addWidget(new QLabel(tr("原文"), this));
    layout->addWidget(m_source, 1);
    layout->addLayout(buttons);
    layout->addWidget(m_urlSafe);
    layout->addWidget(new QLabel(tr("结果"), this));
    layout->addWidget(m_result, 1);

    // 所有连接都以 this 作为上下文对象，页面销毁时连接自动断开。
    connect(encodeButton, &QPushButton::clicked, this, [this] {
        const QByteArray encoded = m_source->toPlainText().toUtf8().toBase64(currentOptions());
        m_result->setPlainText(QString::fromUtf8(encoded));
    });
    connect(decodeButton, &QPushButton::clicked, this, [this] {
        const QByteArray decoded =
            QByteArray::fromBase64(m_source->toPlainText().trimmed().toUtf8(), currentOptions());
        m_result->setPlainText(QString::fromUtf8(decoded));
    });
    connect(swapButton, &QPushButton::clicked, this, [this] {
        m_source->setPlainText(m_result->toPlainText());
        m_result->clear();
    });
    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_source->clear();
        m_result->clear();
    });
}

QByteArray::Base64Options Base64Page::currentOptions() const
{
    return m_urlSafe->isChecked() ? QByteArray::Base64UrlEncoding : QByteArray::Base64Encoding;
}

void Base64Page::restoreState(const toolbox::ToolSettings &settings)
{
    m_urlSafe->setChecked(settings.value(QStringLiteral("urlSafe"), false).toBool());
}

void Base64Page::saveState(const toolbox::ToolSettings &settings)
{
    settings.setValue(QStringLiteral("urlSafe"), m_urlSafe->isChecked());
}

toolbox::ToolMeta Base64Plugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("text.base64");
    info.name = tr("Base64 编解码");
    info.category = tr("文本编码");
    info.version = QStringLiteral("0.2.0");
    info.description = tr("UTF-8 文本与 Base64 互转，支持 URL 安全字符集。");
    // 图标来自插件自身的资源（由 CMakeLists.txt 里的 qt_add_resources 打进 DLL）。
    info.icon = QIcon(QStringLiteral(":/icons/base64.svg"));
    return info;
}

QWidget *Base64Plugin::createPage(QWidget *parent)
{
    return new Base64Page(parent);
}

#include "Base64Plugin.moc"
