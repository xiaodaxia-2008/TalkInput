#include "ocr_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "voice_input_controller.h"

#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace talkinput
{

OcrSettingsWidget::OcrSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

OcrSettingsWidget::~OcrSettingsWidget() = default;

void OcrSettingsWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *groupLayout = new QHBoxLayout();
    groupLayout->setSpacing(8);

    m_providerLabel = new QLabel(content);
    groupLayout->addWidget(m_providerLabel);

    m_ocrCombo = new QComboBox(content);
    m_ocrCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    groupLayout->addWidget(m_ocrCombo, 1);

    contentLayout->addLayout(groupLayout);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    for (const auto &[key, preset] : appConfig().ocrPresets) {
        m_ocrCombo->addItem(QString::fromStdString(preset.name),
                            QString::fromStdString(key));
    }

    connect(m_ocrCombo, &QComboBox::currentIndexChanged, this,
            &OcrSettingsWidget::onOcrProviderChanged);
}

void OcrSettingsWidget::retranslate()
{
    m_providerLabel->setText(tr("Provider:"));
}

void OcrSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void OcrSettingsWidget::refreshFromConfig()
{
    const int index = m_ocrCombo->findData(
        QString::fromStdString(appConfig().settings.ocrProviderId));
    const QSignalBlocker blocker(m_ocrCombo);
    if (index >= 0) {
        m_ocrCombo->setCurrentIndex(index);
    }
}

void OcrSettingsWidget::onOcrProviderChanged(int /*index*/)
{
    appConfig().settings.ocrProviderId =
        m_ocrCombo->currentData().toString().toStdString();
    markConfigDirty();

    if (auto *vc = VoiceInputController::instance()) {
        vc->reloadOcrRecognizer();
    }
}

} // namespace talkinput
