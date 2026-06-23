#include "log_panel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace talkinput
{

LogPanel::LogPanel(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Toolbar ──────────────────────────────────────────────
    auto *toolbar = new QHBoxLayout;
    toolbar->setContentsMargins(4, 4, 4, 2);

    auto *clearBtn = new QPushButton(tr("Clear"), this);
    clearBtn->setFixedHeight(24);
    connect(clearBtn, &QPushButton::clicked, this, &LogPanel::onClear);
    toolbar->addWidget(clearBtn);
    toolbar->addStretch();

    layout->addLayout(toolbar);

    // ── Log text area ────────────────────────────────────────
    m_textEdit = new QPlainTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setFrameShape(QFrame::NoFrame);
    m_textEdit->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_textEdit->setMaximumBlockCount(kMaxLines);
    m_textEdit->setObjectName("logPanelTextEdit");

    layout->addWidget(m_textEdit);
}

LogPanel::~LogPanel() = default;

void LogPanel::onClear()
{
    m_textEdit->clear();
}

} // namespace talkinput
