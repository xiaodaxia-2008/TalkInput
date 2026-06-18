#include "scroll_text_display.h"

#include <QCoreApplication>
#include <QScrollBar>
#include <QVBoxLayout>

ScrollTextDisplay::ScrollTextDisplay(QWidget *parent) : QWidget(parent)
{
    m_placeholder = tr("Listening...");

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName("scrollTextArea");
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setAlignment(Qt::AlignCenter);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_label = new QLabel;
    m_label->setObjectName("scrollTextLabel");
    m_label->setWordWrap(true);
    m_label->setAlignment(Qt::AlignCenter);
    m_label->setText(m_placeholder);

    m_scrollArea->setWidget(m_label);
    outer->addWidget(m_scrollArea);
}

void ScrollTextDisplay::setText(const QString &text)
{
    m_label->setText(text.isEmpty() ? m_placeholder : text);
    m_scrollArea->viewport()->update();
    QCoreApplication::processEvents();
    auto *sb = m_scrollArea->verticalScrollBar();
    if (sb && sb->maximum() > 0) {
        sb->setValue(sb->maximum());
    }
}

void ScrollTextDisplay::setPlaceholder(const QString &text)
{
    m_placeholder = text;
}
