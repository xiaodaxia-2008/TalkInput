#include "scroll_text_display.h"
#include "ui_scroll_text_display.h"

#include <QCoreApplication>
#include <QEvent>
#include <QScrollBar>
#include <QVBoxLayout>

ScrollTextDisplay::ScrollTextDisplay(QWidget *parent) : QWidget(parent)
{
    m_ui = std::make_unique<Ui::ScrollTextDisplay>();
    m_ui->setupUi(this);
    m_placeholder = m_ui->scrollTextLabel->text();
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
    const bool showingPlaceholder =
        m_ui->scrollTextLabel->text() == m_placeholder;
    m_placeholder = text;
    if (showingPlaceholder) {
        m_ui->scrollTextLabel->setText(m_placeholder);
    }
}

void ScrollTextDisplay::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) {
        return;
    }

    const QString currentText = m_ui->scrollTextLabel->text();
    const bool showingPlaceholder = currentText == m_placeholder;
    m_ui->retranslateUi(this);
    m_placeholder = m_ui->scrollTextLabel->text();
    if (!showingPlaceholder) {
        m_ui->scrollTextLabel->setText(currentText);
    }
}
