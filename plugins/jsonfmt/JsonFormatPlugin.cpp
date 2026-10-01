#include "JsonFormatPlugin.h"

#include "core/JsonFormat.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

toolbox::ToolMeta JsonFormatPlugin::meta() const
{
    toolbox::ToolMeta info;
    info.id = QStringLiteral("dev.json-format");
    info.name = tr("JSON 格式化");
    info.category = tr("开发辅助");
    info.version = QStringLiteral("0.1.0");
    info.description = tr("格式化、压缩 JSON，并提示语法错误位置。");
    // 这里故意不设置 icon，外壳会自己生成首字符占位图标。
    return info;
}

QWidget *JsonFormatPlugin::createPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *layout = new QVBoxLayout(page);

    auto *source = new QPlainTextEdit(page);
    source->setPlaceholderText(tr("在这里粘贴 JSON"));

    auto *result = new QPlainTextEdit(page);
    result->setPlaceholderText(tr("结果"));
    result->setReadOnly(true);

    auto *hint = new QLabel(page);

    auto *beautifyButton = new QPushButton(tr("格式化"), page);
    auto *compactButton = new QPushButton(tr("压缩"), page);
    auto *clearButton = new QPushButton(tr("清空"), page);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(beautifyButton);
    buttons->addWidget(compactButton);
    buttons->addStretch(1);
    buttons->addWidget(clearButton);

    layout->addWidget(source, 1);
    layout->addLayout(buttons);
    layout->addWidget(hint);
    layout->addWidget(result, 1);

    // 解析与序列化在 core/JsonFormat.h 里（可单测），这里只把结果翻成界面文案 ——
    // 文案要 tr()，属于 View。
    const auto convert = [source, result, hint](bool compact) {
        const jsonfmt::FormatResult formatted = jsonfmt::formatJson(source->toPlainText(), compact);
        if (!formatted.ok) {
            result->clear();
            hint->setText(
                tr("解析失败：偏移 %1 —— %2").arg(formatted.errorOffset).arg(formatted.errorText));
            return;
        }
        hint->clear();
        result->setPlainText(formatted.text);
    };

    connect(beautifyButton, &QPushButton::clicked, page, [convert] { convert(false); });
    connect(compactButton, &QPushButton::clicked, page, [convert] { convert(true); });
    connect(clearButton, &QPushButton::clicked, page, [source, result, hint] {
        source->clear();
        result->clear();
        hint->clear();
    });

    return page;
}
