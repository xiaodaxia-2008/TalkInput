#include "general_settings_widget.h"
#include "ui_general_settings_widget.h"

#include <QEvent>
#include <QPushButton>

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
    m_ui = std::make_unique<Ui::GeneralSettingsWidget>();
    m_ui->setupUi(this);
    connect(m_ui->resetButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::resetSettingsRequested);
    connect(m_ui->dataDirectoryButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::openDataDirectoryRequested);
    connect(m_ui->aboutButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::aboutRequested);
    connect(m_ui->exitButton, &QPushButton::clicked, this,
            &GeneralSettingsWidget::exitRequested);
}

void GeneralSettingsWidget::retranslate()
{
    m_ui->retranslateUi(this);
}

void GeneralSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

} // namespace talkinput
