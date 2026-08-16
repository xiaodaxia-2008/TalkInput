#include "general_settings_widget.h"

#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace talkinput
{

GeneralSettingsWidget::GeneralSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
}

GeneralSettingsWidget::~GeneralSettingsWidget() = default;

void GeneralSettingsWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    m_group = new QGroupBox(content);
    auto *groupLayout = new QVBoxLayout(m_group);
    groupLayout->setContentsMargins(16, 20, 16, 14);
    groupLayout->setSpacing(10);

    m_resetButton = new QPushButton(m_group);
    m_dataDirectoryButton = new QPushButton(m_group);
    m_aboutButton = new QPushButton(m_group);
    m_exitButton = new QPushButton(m_group);
    groupLayout->addWidget(m_resetButton);
    groupLayout->addWidget(m_dataDirectoryButton);
    groupLayout->addWidget(m_aboutButton);
    groupLayout->addWidget(m_exitButton);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    connect(m_resetButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::resetSettingsRequested);
    connect(m_dataDirectoryButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::openDataDirectoryRequested);
    connect(m_aboutButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::aboutRequested);
    connect(m_exitButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::exitRequested);
}

void GeneralSettingsWidget::retranslate()
{
    m_group->setTitle(tr("General"));
    m_resetButton->setText(tr("Reset Settings"));
    m_dataDirectoryButton->setText(tr("Open Data Directory"));
    m_aboutButton->setText(tr("About"));
    m_exitButton->setText(tr("Exit"));
}

void GeneralSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

} // namespace talkinput
