#include "Base64Plugin.h"

#include "core/Base64Codec.h"

#include <QCheckBox>
#include <QClipboard>
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

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 拖进来的文件最多读 1 MB。再大只会把界面卡住，而这个工具本来就是处理文本的。
    static constexpr qint64 kMaxDropBytes = 1024 * 1024;

    /// 把解码失败的原因翻成人话。
    ///
    /// 之所以值得单独一个函数：Qt 的解码器对非法输入是宽容的（跳过不认识的字符
    /// 照解），改之前界面会把「输入压根不是 Base64」显示成一串问号，用户只会以为
    /// 是自己粘错了。现在失败要说出**是哪一种失败**，这话只有界面该说（它是文案），
    /// 判定本身在 core 里。
    QString decodeFailureText(base64::DecodeError error, const QString &input, bool urlSafe) const;
    void readDroppedFile(const QString &path);

    QPlainTextEdit *m_source = nullptr;
    QPlainTextEdit *m_result = nullptr;
    QCheckBox *m_urlSafe = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_copy = nullptr;
};

Base64Page::Base64Page(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);

    m_source = new QPlainTextEdit(this);
    m_source->setPlaceholderText(tr("在这里输入原文，或粘贴 Base64 —— 也可以把文本文件拖进来"));

    m_result = new QPlainTextEdit(this);
    m_result->setPlaceholderText(tr("结果"));
    m_result->setReadOnly(true);

    // 结果为空时不允许复制：点了没反应比按钮灰着更让人困惑。
    m_copy = new QPushButton(tr("复制结果"), this);
    m_copy->setEnabled(false);

    auto *encodeButton = new QPushButton(tr("编码 →"), this);
    auto *decodeButton = new QPushButton(tr("← 解码"), this);
    auto *swapButton = new QPushButton(tr("结果转原文"), this);
    auto *clearButton = new QPushButton(tr("清空"), this);

    // 快捷键写在 tooltip 里，否则没人知道它们存在。
    encodeButton->setToolTip(tr("编码（Ctrl+Enter）"));
    decodeButton->setToolTip(tr("解码（Ctrl+Shift+Enter）"));

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(encodeButton);
    buttons->addWidget(decodeButton);
    buttons->addStretch(1);
    buttons->addWidget(m_copy);
    buttons->addWidget(swapButton);
    buttons->addWidget(clearButton);

    m_urlSafe = new QCheckBox(tr("URL 安全字符集（用 - _ 代替 + /）"), this);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);

    layout->addWidget(new QLabel(tr("原文"), this));
    layout->addWidget(m_source, 1);
    layout->addLayout(buttons);
    layout->addWidget(m_urlSafe);
    layout->addWidget(m_hint);
    layout->addWidget(new QLabel(tr("结果"), this));
    layout->addWidget(m_result, 1);

    // 编解码规则在 base64_core（可单测），这里只负责取文本、传参、显示结果。
    // 所有连接都以 this 作为上下文对象，页面销毁时连接自动断开。
    connect(encodeButton, &QPushButton::clicked, this, [this] {
        m_hint->clear();
        m_result->setPlainText(
            base64::encodeBase64(m_source->toPlainText(), m_urlSafe->isChecked()));
    });

    connect(decodeButton, &QPushButton::clicked, this, [this] {
        const bool urlSafe = m_urlSafe->isChecked();
        const base64::DecodeResult decoded =
            base64::decodeBase64Checked(m_source->toPlainText(), urlSafe);
        if (!decoded.ok) {
            m_result->clear();
            m_hint->setText(decodeFailureText(decoded.error, m_source->toPlainText(), urlSafe));
            return;
        }
        m_hint->clear();
        m_result->setPlainText(decoded.text);
    });

    // 大回车（Enter）与小键盘回车（Return）是同一个键位在不同键盘上的两种叫法，
    // 只接一个的话，用另一块键盘的人会觉得快捷键时灵时不灵。
    for (const auto &keys : {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter))}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, encodeButton, &QPushButton::click);
    }
    for (const auto &keys :
         {QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Return)),
          QKeySequence(QKeyCombination(Qt::ControlModifier | Qt::ShiftModifier, Qt::Key_Enter))}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, decodeButton, &QPushButton::click);
    }

    connect(m_copy, &QPushButton::clicked, this, [this] {
        const QString text = m_result->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        // 说一句「复制好了」：剪贴板没有任何视觉反馈，不说用户会怀疑有没有生效。
        m_hint->setText(tr("已复制结果到剪贴板。"));
    });

    // 复制按钮的可用状态跟着结果走，省得在每处 setPlainText 之后单独维护。
    connect(m_result, &QPlainTextEdit::textChanged, this,
            [this] { m_copy->setEnabled(!m_result->toPlainText().isEmpty()); });

    connect(swapButton, &QPushButton::clicked, this, [this] {
        m_source->setPlainText(m_result->toPlainText());
        m_result->clear();
        m_hint->clear();
    });

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_source->clear();
        m_result->clear();
        m_hint->clear();
    });
}

void Base64Page::dragEnterEvent(QDragEnterEvent *event)
{
    // 只接文件：拖一段 URL 文本进来放进原文框没有意义。
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void Base64Page::dropEvent(QDropEvent *event)
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

void Base64Page::readDroppedFile(const QString &path)
{
    const QFileInfo info(path);

    if (info.size() > kMaxDropBytes) {
        m_hint->setText(tr("「%1」有 %2 KB，超过 1 MB —— 这工具是处理文本的，"
                           "再大只会把界面卡住。")
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
    // 二进制文件读进来会变成一堆替换字符，与其给出看着像结果的东西，不如直说读不了。
    if (bytes.contains('\0')) {
        m_hint->setText(tr("「%1」不像文本文件（含二进制内容）。").arg(info.fileName()));
        return;
    }

    m_source->setPlainText(QString::fromUtf8(bytes));
    m_hint->setText(tr("已读入 %1（%2 字节）").arg(info.fileName()).arg(bytes.size()));
}

QString Base64Page::decodeFailureText(base64::DecodeError error, const QString &input,
                                      bool urlSafe) const
{
    switch (error) {
    case base64::DecodeError::Empty:
        return tr("先在原文框里粘贴要解码的 Base64。");

    case base64::DecodeError::BadLength:
        return tr("不像 Base64：去掉首尾空白后是 %1 个字符，必须能被 4 整除"
                  "（多半是粘贴时被截断了）。")
            .arg(input.trimmed().size());

    case base64::DecodeError::BadCharacter:
        // 最常见的一种错：字符集勾错了。URL 安全的 `-` `_` 在标准字符集里就是
        // 非法字符，只说「含非法字符」用户还得自己猜。
        if (!urlSafe && (input.contains(QLatin1Char('-')) || input.contains(QLatin1Char('_')))) {
            return tr("含标准字符集之外的字符（- 或 _）。这串像是 URL 安全 Base64，"
                      "试试勾上「URL 安全字符集」。");
        }
        return tr("含当前字符集之外的字符，看起来不是有效的 Base64。");

    case base64::DecodeError::None:
        break;
    }

    return {};
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
    info.version = QStringLiteral("0.3.0");
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
