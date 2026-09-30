#include "JsonFormatPlugin.h"

#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

toolbox::ToolMeta JsonFormatPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("dev.json-format");
    info.name = QStringLiteral("JSON 格式化");
    info.category = QStringLiteral("开发辅助");
    info.version = QStringLiteral("0.1.0");
    info.description = QStringLiteral("格式化、压缩 JSON，并提示语法错误位置。");
    // 这里故意不设置 icon，外壳会自己生成首字符占位图标。
    return info;
}

QWidget *JsonFormatPlugin::createPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);

    auto *source = new QPlainTextEdit(page);
    source->setPlaceholderText(QStringLiteral("在这里粘贴 JSON"));

    auto *result = new QPlainTextEdit(page);
    result->setPlaceholderText(QStringLiteral("结果"));
    result->setReadOnly(true);

    auto *hint = new QLabel(page);

    auto *beautifyButton = new QPushButton(QStringLiteral("格式化"), page);
    auto *compactButton = new QPushButton(QStringLiteral("压缩"), page);
    auto *clearButton = new QPushButton(QStringLiteral("清空"), page);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(beautifyButton);
    buttons->addWidget(compactButton);
    buttons->addStretch(1);
    buttons->addWidget(clearButton);

    layout->addWidget(source, 1);
    layout->addLayout(buttons);
    layout->addWidget(hint);
    layout->addWidget(result, 1);

    const auto convert = [source, result, hint](QJsonDocument::JsonFormat format) {
        QJsonParseError error{};
        const QJsonDocument document =
            QJsonDocument::fromJson(source->toPlainText().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            result->clear();
            hint->setText(tr("解析失败：偏移 %1 —— %2")
                              .arg(error.offset)
                              .arg(error.errorString()));
            return;
        }
        hint->clear();
        result->setPlainText(QString::fromUtf8(document.toJson(format)));
    };

    connect(beautifyButton, &QPushButton::clicked, page, [convert] {
        convert(QJsonDocument::Indented);
    });
    connect(compactButton, &QPushButton::clicked, page, [convert] {
        convert(QJsonDocument::Compact);
    });
    connect(clearButton, &QPushButton::clicked, page, [source, result, hint] {
        source->clear();
        result->clear();
        hint->clear();
    });

    return page;
}