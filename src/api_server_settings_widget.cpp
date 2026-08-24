#include "api_server_settings_widget.h"
#include "app_config.h"
#include "speech_api_server.h"
#include "ui_api_server_settings_widget.h"

#include <QCheckBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

namespace talkinput
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
    m_ui->enableCheck->setText(tr("Enable local API server"));
    m_ui->enableCheck->setToolTip(tr(
        "Expose the loaded ASR and OCR providers through an OpenAI-compatible "
        "HTTP API"));
    m_ui->hostLabel->setText(tr("Host"));
    m_ui->portLabel->setText(tr("Port"));
    m_ui->keyLabel->setText(tr("API Key"));
    m_ui->keyLabel->setToolTip(
        tr("Leave empty to allow requests without authentication"));
    m_ui->apiInfoEdit->setPlainText(
        tr("Local API endpoints:\n"
           "GET /, /health, /healthz — health check\n"
           "GET /v1/models — list the currently loaded recognition model\n"
           "POST /v1/audio/transcriptions — transcribe an audio file "
           "(multipart/form-data)\n"
           "POST /v1/ocr — recognize text in an image "
           "(multipart/form-data)\n"
           "POST /v1/images/ocr — alias for /v1/ocr\n"
           "POST /v1/audio/speech — convert text to speech (JSON)"));
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
    if (auto *server = SpeechApiServer::instance()) {
        server->applySettings();
    }
}

} // namespace talkinput
