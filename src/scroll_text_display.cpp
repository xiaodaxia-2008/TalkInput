#include "scroll_text_display.h"

#include <QCoreApplication>
#include <QEvent>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

ScrollTextDisplay::ScrollTextDisplay(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_scrollTextArea = new QScrollArea(this);
    m_scrollTextArea->setWidgetResizable(true);
    m_scrollTextArea->setFrameShape(QFrame::NoFrame);
    m_scrollTextArea->setAlignment(Qt::AlignCenter);
    m_scrollTextArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollTextArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_scrollTextLabel = new QLabel(tr("Recording..."));
    m_scrollTextLabel->setWordWrap(true);
    m_scrollTextLabel->setAlignment(Qt::AlignCenter);
    m_scrollTextArea->setWidget(m_scrollTextLabel);
    layout->addWidget(m_scrollTextArea);

    m_placeholder = m_scrollTextLabel->text();
}

ScrollTextDisplay::~ScrollTextDisplay() = default;

void ScrollTextDisplay::setText(const QString &text)
{
    m_scrollTextLabel->setText(text.isEmpty() ? m_placeholder : text);
    m_scrollTextArea->viewport()->update();
    QCoreApplication::processEvents();
    auto *sb = m_scrollTextArea->verticalScrollBar();
    if (sb && sb->maximum() > 0) {
        sb->setValue(sb->maximum());
    }
}

void ScrollTextDisplay::setPlaceholder(const QString &text)
{
    const bool showingPlaceholder = m_scrollTextLabel->text() == m_placeholder;
    m_placeholder = text;
    if (showingPlaceholder) {
        m_scrollTextLabel->setText(m_placeholder);
    }
}

void ScrollTextDisplay::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) {
        return;
    }

    const QString currentText = m_scrollTextLabel->text();
    const bool showingPlaceholder = currentText == m_placeholder;
    m_placeholder = tr("Recording...");
    if (!showingPlaceholder) {
        m_scrollTextLabel->setText(currentText);
    }
    else {
        m_scrollTextLabel->setText(m_placeholder);
    }
}
