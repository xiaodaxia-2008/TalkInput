#include "log_panel.h"
#include "ui_log_panel.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

namespace zenny
{

LogPanel::LogPanel(QWidget *parent) : QWidget(parent)
{
    m_ui = std::make_unique<Ui::LogPanel>();
    m_ui->setupUi(this);
    m_ui->layout->setContentsMargins(12, 12, 12, 12);
    m_ui->layout->setSpacing(8);
    m_ui->toolbar->setContentsMargins(0, 0, 0, 0);
    m_ui->logPanelTextEdit->setMaximumBlockCount(kMaxLines);
    connect(m_ui->clearButton, &QPushButton::clicked, this,
            &LogPanel::onClear);
}

LogPanel::~LogPanel() = default;

void LogPanel::onClear()
{
    m_ui->logPanelTextEdit->clear();
}

QPlainTextEdit *LogPanel::textEdit() const
{
    return m_ui->logPanelTextEdit;
}

} // namespace zenny
