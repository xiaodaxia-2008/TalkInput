#include "api_server_settings_widget.h"
#include "app_config.h"
#include "speech_api_server.h"

#include <QCheckBox>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
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
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    m_group = new QGroupBox(content);
    auto *grid = new QGridLayout(m_group);
    grid->setContentsMargins(16, 20, 16, 14);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);

    m_enableCheck = new QCheckBox(m_group);
    grid->addWidget(m_enableCheck, 0, 0, 1, 2);

    m_hostLabel = new QLabel(m_group);
    grid->addWidget(m_hostLabel, 1, 0);

    m_hostEdit = new QLineEdit(m_group);
    m_hostEdit->setPlaceholderText(QStringLiteral("127.0.0.1"));
    m_hostEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(m_hostEdit, 1, 1);

    m_portLabel = new QLabel(m_group);
    grid->addWidget(m_portLabel, 2, 0);

    m_portSpin = new QSpinBox(m_group);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(8766);
    m_portSpin->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    grid->addWidget(m_portSpin, 2, 1);

    m_keyLabel = new QLabel(m_group);
    m_keyLabel->setToolTip(
        tr("Leave empty to allow requests without authentication"));
    grid->addWidget(m_keyLabel, 3, 0);

    m_keyEdit = new QLineEdit(m_group);
    m_keyEdit->setEchoMode(QLineEdit::Password);
    m_keyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(m_keyEdit, 3, 1);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    connect(m_enableCheck, &QCheckBox::toggled, this, [this]() {
        appConfig().settings.apiServerEnabled = m_enableCheck->isChecked();
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_hostEdit, &QLineEdit::editingFinished, this, [this]() {
        appConfig().settings.apiServerHost =
            m_hostEdit->text().trimmed().toStdString();
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_portSpin, &QSpinBox::valueChanged, this, [this](int value) {
        appConfig().settings.apiServerPort = value;
        markConfigDirty();
        applyApiServerSettings();
    });
    connect(m_keyEdit, &QLineEdit::editingFinished, this, [this]() {
        appConfig().settings.apiServerApiKey =
            m_keyEdit->text().trimmed().toStdString();
        markConfigDirty();
        applyApiServerSettings();
    });
}

void ApiServerSettingsWidget::retranslate()
{
    m_group->setTitle(tr("API Server"));
    m_enableCheck->setText(tr("Enable local API server"));
    m_enableCheck->setToolTip(tr(
        "Expose the loaded ASR model through an OpenAI-compatible HTTP API"));
    m_hostLabel->setText(tr("Host"));
    m_portLabel->setText(tr("Port"));
    m_keyLabel->setText(tr("API Key"));
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
    const QSignalBlocker bc1(m_enableCheck);
    const QSignalBlocker bc2(m_hostEdit);
    const QSignalBlocker bc3(m_portSpin);
    const QSignalBlocker bc4(m_keyEdit);
    m_enableCheck->setChecked(appConfig().settings.apiServerEnabled);
    m_hostEdit->setText(
        QString::fromStdString(appConfig().settings.apiServerHost));
    m_portSpin->setValue(appConfig().settings.apiServerPort);
    m_keyEdit->setText(
        QString::fromStdString(appConfig().settings.apiServerApiKey));
}

void ApiServerSettingsWidget::applyApiServerSettings()
{
    if (auto *server = SpeechApiServer::instance()) {
        server->applySettings();
    }
}

} // namespace talkinput
