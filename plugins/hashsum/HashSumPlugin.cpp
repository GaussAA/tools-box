#include "HashSumPlugin.h"

#include "core/HashSum.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequence>
#include <QLabel>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

/// 工具页面：哈希与校验和。
///
/// 它除了是界面，还实现了 toolbox::IToolPage —— 外壳会在页面创建后调用
/// restoreState()，在窗口关闭或重载插件前调用 saveState()，所以插件自己
/// 不用操心 QSettings 的键名，也不会和别的插件串键。
class HashSumPage : public QWidget, public toolbox::IToolPage
{
    Q_OBJECT
    Q_INTERFACES(toolbox::IToolPage)

public:
    explicit HashSumPage(QWidget *parent = nullptr);

    void restoreState(const toolbox::ToolSettings &settings) override;
    void saveState(const toolbox::ToolSettings &settings) override;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    /// 文件模式的上限。再大就该做真正的流式 + 后台线程了，那是另一件事；
    /// 这里的分块读取只是保证「不把整份文件塞进内存」。
    static constexpr qint64 kMaxFileBytes = 32 * 1024 * 1024;

    void compute();
    void setFilePath(const QString &path);
    /// 两个输入区只显示当前模式用的那个，免得界面上摆着一个用不上的控件。
    void syncInputVisibility();
    QList<hashsum::Algorithm> selectedAlgorithms() const;
    QString fileErrorText(hashsum::FileError error) const;

    QRadioButton *m_textMode = nullptr;
    QRadioButton *m_fileMode = nullptr;
    QPlainTextEdit *m_text = nullptr;
    QWidget *m_fileBox = nullptr;
    QLabel *m_fileLabel = nullptr;
    QList<QCheckBox *> m_boxes; ///< 顺序与 hashsum::allAlgorithms() 一致
    QPlainTextEdit *m_result = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_copy = nullptr;
    QString m_filePath;
};

HashSumPage::HashSumPage(QWidget *parent)
    : QWidget(parent)
{
    // 这个工具最顺手的用法就是把文件拖进来。
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);

    m_textMode = new QRadioButton(tr("文本"), this);
    m_fileMode = new QRadioButton(tr("文件"), this);
    m_textMode->setChecked(true);

    auto *modeRow = new QHBoxLayout;
    modeRow->addWidget(m_textMode);
    modeRow->addWidget(m_fileMode);
    modeRow->addStretch(1);

    m_text = new QPlainTextEdit(this);
    m_text->setPlaceholderText(tr("在这里输入要计算摘要的文本"));

    m_fileBox = new QWidget(this);
    auto *fileRow = new QHBoxLayout(m_fileBox);
    fileRow->setContentsMargins(0, 0, 0, 0);
    m_fileLabel = new QLabel(tr("尚未选择文件 —— 也可以把文件拖进来"), m_fileBox);
    auto *pickButton = new QPushButton(tr("选择文件…"), m_fileBox);
    fileRow->addWidget(m_fileLabel, 1);
    fileRow->addWidget(pickButton);

    // 勾选框由 core 的算法列表生成：新增算法时界面会自己长出来，不会漏。
    auto *algoRow = new QHBoxLayout;
    for (const hashsum::Algorithm algo : hashsum::allAlgorithms()) {
        auto *box = new QCheckBox(hashsum::algorithmName(algo), this);
        box->setChecked(true);
        algoRow->addWidget(box);
        m_boxes.append(box);
    }
    algoRow->addStretch(1);

    auto *runButton = new QPushButton(tr("计算"), this);
    runButton->setToolTip(tr("计算（Ctrl+Enter）"));
    m_copy = new QPushButton(tr("复制结果"), this);
    m_copy->setEnabled(false);
    auto *clearButton = new QPushButton(tr("清空"), this);

    auto *buttonRow = new QHBoxLayout;
    buttonRow->addWidget(runButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_copy);
    buttonRow->addWidget(clearButton);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);

    m_result = new QPlainTextEdit(this);
    m_result->setPlaceholderText(tr("结果"));
    m_result->setReadOnly(true);

    layout->addLayout(modeRow);
    layout->addWidget(m_text, 1);
    layout->addWidget(m_fileBox);
    layout->addLayout(algoRow);
    layout->addLayout(buttonRow);
    layout->addWidget(m_hint);
    layout->addWidget(m_result, 1);

    connect(m_textMode, &QRadioButton::toggled, this, [this](bool on) {
        if (on) {
            syncInputVisibility();
        }
    });
    connect(m_fileMode, &QRadioButton::toggled, this, [this](bool on) {
        if (on) {
            syncInputVisibility();
        }
    });

    connect(pickButton, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("选择文件"));
        if (path.isEmpty()) {
            return;
        }
        setFilePath(path);
        compute(); // 选完就算，省掉一次点击
    });

    connect(runButton, &QPushButton::clicked, this, [this] { compute(); });

    // 大回车与小键盘回车各接一个，否则用另一块键盘的人会觉得快捷键时灵时不灵。
    for (const auto &keys : {QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Return)),
                             QKeySequence(QKeyCombination(Qt::ControlModifier, Qt::Key_Enter))}) {
        auto *shortcut = new QShortcut(keys, this);
        connect(shortcut, &QShortcut::activated, runButton, &QPushButton::click);
    }

    connect(m_copy, &QPushButton::clicked, this, [this] {
        const QString text = m_result->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        m_hint->setText(tr("已复制结果到剪贴板。"));
    });

    connect(m_result, &QPlainTextEdit::textChanged, this,
            [this] { m_copy->setEnabled(!m_result->toPlainText().isEmpty()); });

    connect(clearButton, &QPushButton::clicked, this, [this] {
        m_text->clear();
        m_result->clear();
        m_hint->clear();
        m_filePath.clear();
        m_fileLabel->setText(tr("尚未选择文件 —— 也可以把文件拖进来"));
    });

    syncInputVisibility();
}

void HashSumPage::syncInputVisibility()
{
    const bool fileMode = m_fileMode->isChecked();
    m_text->setVisible(!fileMode);
    m_fileBox->setVisible(fileMode);
}

QList<hashsum::Algorithm> HashSumPage::selectedAlgorithms() const
{
    const QList<hashsum::Algorithm> all = hashsum::allAlgorithms();
    QList<hashsum::Algorithm> picked;
    for (int i = 0; i < m_boxes.size() && i < all.size(); ++i) {
        if (m_boxes.at(i)->isChecked()) {
            picked.append(all.at(i));
        }
    }
    return picked;
}

QString HashSumPage::fileErrorText(hashsum::FileError error) const
{
    switch (error) {
    case hashsum::FileError::NotAFile:
        return tr("这个路径不是文件 —— 可能已被移动或删除。");
    case hashsum::FileError::CannotOpen:
        return tr("打不开这个文件，检查是否有读取权限。");
    case hashsum::FileError::TooLarge:
        return tr("文件超过 %1 MB 的上限。").arg(kMaxFileBytes / (1024 * 1024));
    case hashsum::FileError::None:
        break;
    }
    return {};
}

void HashSumPage::compute()
{
    const QList<hashsum::Algorithm> picked = selectedAlgorithms();
    if (picked.isEmpty()) {
        m_result->clear();
        m_hint->setText(tr("至少勾一个算法。"));
        return;
    }

    QStringList lines;
    if (m_fileMode->isChecked()) {
        if (m_filePath.isEmpty()) {
            m_result->clear();
            m_hint->setText(tr("先选一个文件，或把文件拖进来。"));
            return;
        }
        for (const hashsum::Algorithm algo : picked) {
            const hashsum::HashResult digest = hashsum::hashFile(m_filePath, algo, kMaxFileBytes);
            if (!digest.ok) {
                m_result->clear();
                m_hint->setText(fileErrorText(digest.error));
                return;
            }
            lines.append(QStringLiteral("%1  %2").arg(hashsum::algorithmName(algo), digest.text));
        }
    } else {
        // 与 base64 同一条约定：QString 内部是 UTF-16，取字节必须显式 toUtf8()，
        // 否则中文会算出与别处对不上的值。
        const QByteArray payload = m_text->toPlainText().toUtf8();
        for (const hashsum::Algorithm algo : picked) {
            lines.append(QStringLiteral("%1  %2").arg(hashsum::algorithmName(algo),
                                                      hashsum::hashBytes(payload, algo)));
        }
    }

    m_hint->clear();
    m_result->setPlainText(lines.join(QLatin1Char('\n')));
}

void HashSumPage::setFilePath(const QString &path)
{
    m_filePath = path;
    m_fileLabel->setText(QFileInfo(path).fileName());
    // 拖进文件时不该要求用户再点一下「文件」模式。
    m_fileMode->setChecked(true);
}

void HashSumPage::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void HashSumPage::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) {
        return;
    }
    const QString path = urls.first().toLocalFile();
    if (path.isEmpty()) {
        return;
    }
    setFilePath(path);
    // 拖进来就算：少一次点击，且拖放本身已经表达了「我要算这个」。
    compute();
    event->acceptProposedAction();
}

void HashSumPage::restoreState(const toolbox::ToolSettings &settings)
{
    const QList<hashsum::Algorithm> all = hashsum::allAlgorithms();
    const QStringList picked = settings.value(QStringLiteral("algorithms")).toStringList();

    if (picked.isEmpty()) {
        // 没有记录就维持构造时的默认（全选）。
        return;
    }
    for (int i = 0; i < m_boxes.size() && i < all.size(); ++i) {
        m_boxes.at(i)->setChecked(picked.contains(hashsum::algorithmName(all.at(i))));
    }

    // 存的是算法名而不是下标：将来插入新算法时，老用户的勾选不会错位。
    if (settings.value(QStringLiteral("mode")).toString() == QStringLiteral("file")) {
        m_fileMode->setChecked(true);
    } else {
        m_textMode->setChecked(true);
    }
    syncInputVisibility();
}

void HashSumPage::saveState(const toolbox::ToolSettings &settings)
{
    const QList<hashsum::Algorithm> all = hashsum::allAlgorithms();
    QStringList picked;
    for (int i = 0; i < m_boxes.size() && i < all.size(); ++i) {
        if (m_boxes.at(i)->isChecked()) {
            picked.append(hashsum::algorithmName(all.at(i)));
        }
    }
    settings.setValue(QStringLiteral("algorithms"), picked);
    settings.setValue(QStringLiteral("mode"),
                      m_fileMode->isChecked() ? QStringLiteral("file") : QStringLiteral("text"));
}

toolbox::ToolMeta HashSumPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("dev.hash-sum");
    info.name = tr("哈希与校验和");
    info.category = tr("开发辅助");
    info.version = QStringLiteral("0.1.0");
    info.description = tr("对文本或文件计算 MD5 / SHA-1 / SHA-256 / SHA-512。");
    info.icon = QIcon(QStringLiteral(":/icons/hashsum.svg"));
    return info;
}

QWidget *HashSumPlugin::createPage(QWidget *parent)
{
    return new HashSumPage(parent);
}

#include "HashSumPlugin.moc"
