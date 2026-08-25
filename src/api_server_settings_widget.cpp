#include "api_server_settings_widget.h"
#include "app_config.h"
#include "local_ai_api_server.h"
#include "ui_api_server_settings_widget.h"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace zenny
{

ApiServerSettingsWidget::ApiServerSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

ApiServerSettingsWidget::~ApiServerSettingsWidget() = default;

void ApiServerSettingsWidget::buildUi()
{
    m_ui = std::make_unique<Ui::ApiServerSettingsWidget>();
    m_ui->setupUi(this);

    connect(m_ui->enableCheck, &QCheckBox::toggled, this, [this]() {
        appConfig().settings.apiServerEnabled = m_ui->enableCheck->isChecked();
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_ui->hostEdit, &QLineEdit::editingFinished, this, [this]() {
        appConfig().settings.apiServerHost =
            m_ui->hostEdit->text().trimmed().toStdString();
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_ui->portSpin, &QSpinBox::valueChanged, this, [this](int value) {
        appConfig().settings.apiServerPort = value;
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_ui->keyEdit, &QLineEdit::editingFinished, this, [this]() {
        appConfig().settings.apiServerApiKey =
            m_ui->keyEdit->text().trimmed().toStdString();
        markConfigDirty();
        applyApiServerSettings();
    });
}

void ApiServerSettingsWidget::retranslate()
{
    m_ui->retranslateUi(this);
}

void ApiServerSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void ApiServerSettingsWidget::refreshFromConfig()
{
    const QSignalBlocker bc1(m_ui->enableCheck);
    const QSignalBlocker bc2(m_ui->hostEdit);
    const QSignalBlocker bc3(m_ui->portSpin);
    const QSignalBlocker bc4(m_ui->keyEdit);
    m_ui->enableCheck->setChecked(appConfig().settings.apiServerEnabled);
    m_ui->hostEdit->setText(
        QString::fromStdString(appConfig().settings.apiServerHost));
    m_ui->portSpin->setValue(appConfig().settings.apiServerPort);
    m_ui->keyEdit->setText(
        QString::fromStdString(appConfig().settings.apiServerApiKey));
}

void ApiServerSettingsWidget::applyApiServerSettings()
{
    if (auto *server = LocalAiApiServer::instance()) {
        server->applySettings();
    }
}

} // namespace zenny
