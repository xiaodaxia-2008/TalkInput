#include "scroll_text_display.h"
#include "ui_scroll_text_display.h"

#include <QCoreApplication>
#include <QScrollBar>
#include <QVBoxLayout>

ScrollTextDisplay::ScrollTextDisplay(QWidget *parent) : QWidget(parent)
{
    m_placeholder = tr("Recording...");

    m_ui = std::make_unique<Ui::ScrollTextDisplay>();
    m_ui->setupUi(this);
    m_ui->scrollTextLabel->setText(m_placeholder);
}

ScrollTextDisplay::~ScrollTextDisplay() = default;

void ScrollTextDisplay::setText(const QString &text)
{
    m_ui->scrollTextLabel->setText(text.isEmpty() ? m_placeholder : text);
    m_ui->scrollTextArea->viewport()->update();
    QCoreApplication::processEvents();
    auto *sb = m_ui->scrollTextArea->verticalScrollBar();
    if (sb && sb->maximum() > 0) {
        sb->setValue(sb->maximum());
    }
}

void ScrollTextDisplay::setPlaceholder(const QString &text)
{
    m_placeholder = text;
}
