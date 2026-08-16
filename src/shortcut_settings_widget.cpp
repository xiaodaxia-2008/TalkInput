#include "shortcut_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "voice_input_controller.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace talkinput
{

ShortcutSettingsWidget::ShortcutSettingsWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    retranslate();
    initActiveMode();
    initShortcuts();
    refreshFromConfig();
}

ShortcutSettingsWidget::~ShortcutSettingsWidget() = default;

void ShortcutSettingsWidget::buildUi()
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
    auto *form = new QFormLayout(m_group);
    form->setContentsMargins(16, 20, 16, 14);
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(10);

    m_activeModeLabel = new QLabel(m_group);
    m_activeModeLabel->setToolTip(
        tr("Default pipeline mode for the trigger hotkey"));
    m_activeModeCombo = new QComboBox(m_group);
    m_activeModeCombo->setSizePolicy(QSizePolicy::Expanding,
                                     QSizePolicy::Fixed);
    form->addRow(m_activeModeLabel, m_activeModeCombo);

    m_triggerLabel = new QLabel(m_group);
    m_triggerLabel->setToolTip(
        tr("Global hotkey to trigger the current active mode"));
    auto *triggerRow = new QHBoxLayout;
    triggerRow->setSpacing(4);
    m_triggerEdit = new QKeySequenceEdit(m_group);
    triggerRow->addWidget(m_triggerEdit);
    m_triggerApplyBtn = new QPushButton(QStringLiteral("✓"), m_group);
    m_triggerApplyBtn->setToolTip(tr("Apply shortcut"));
    m_triggerApplyBtn->setFlat(true);
    m_triggerApplyBtn->setFixedWidth(28);
    triggerRow->addWidget(m_triggerApplyBtn);
    form->addRow(m_triggerLabel, triggerRow);

    m_modeSwitchLabel = new QLabel(m_group);
    m_modeSwitchLabel->setToolTip(
        tr("Global hotkey to cycle the active pipeline mode"));
    auto *modeSwitchRow = new QHBoxLayout;
    modeSwitchRow->setSpacing(4);
    m_modeSwitchEdit = new QKeySequenceEdit(m_group);
    modeSwitchRow->addWidget(m_modeSwitchEdit);
    m_modeSwitchApplyBtn = new QPushButton(QStringLiteral("✓"), m_group);
    m_modeSwitchApplyBtn->setToolTip(tr("Apply shortcut"));
    m_modeSwitchApplyBtn->setFlat(true);
    m_modeSwitchApplyBtn->setFixedWidth(28);
    modeSwitchRow->addWidget(m_modeSwitchApplyBtn);
    form->addRow(m_modeSwitchLabel, modeSwitchRow);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);
}

void ShortcutSettingsWidget::retranslate()
{
    m_group->setTitle(tr("Shortcuts"));
    m_activeModeLabel->setText(tr("Active Mode"));
    m_triggerLabel->setText(tr("Trigger"));
    m_modeSwitchLabel->setText(tr("Mode Switch"));
    m_triggerApplyBtn->setToolTip(tr("Apply shortcut"));
    m_modeSwitchApplyBtn->setToolTip(tr("Apply shortcut"));

    const QSignalBlocker blocker(m_activeModeCombo);
    const QString current = m_activeModeCombo->currentData().toString();
    m_activeModeCombo->clear();
    m_activeModeCombo->addItem(tr("ASR only"), QStringLiteral("asr_only"));
    m_activeModeCombo->addItem(tr("ASR + AI Polish"),
                               QStringLiteral("asr_llm"));
    m_activeModeCombo->addItem(tr("ASR + OCR context + AI Polish"),
                               QStringLiteral("asr_llm_ocr"));
    const int idx = m_activeModeCombo->findData(current);
    if (idx >= 0) {
        m_activeModeCombo->setCurrentIndex(idx);
    }
}

void ShortcutSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void ShortcutSettingsWidget::initActiveMode()
{
    const QString activeMode =
        QString::fromStdString(appConfig().settings.activeMode);
    const int idx = m_activeModeCombo->findData(activeMode);
    if (idx >= 0) {
        m_activeModeCombo->setCurrentIndex(idx);
    }

    connect(m_activeModeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const QString mode = m_activeModeCombo->currentData().toString();
        appConfig().settings.activeMode = mode.toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("Active mode changed to %1")
                                 .arg(m_activeModeCombo->currentText()));
    });
}

void ShortcutSettingsWidget::initShortcuts()
{
    // Trigger hotkey — save and re-register
    auto applyTrigger = [this]() {
        appConfig().settings.triggerHotkey =
            m_triggerEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoiceInputController::instance()) {
            ctrl->reregisterTriggerHotkey();
        }
        STATUSBAR_INFO("{}", tr("Trigger shortcut applied"));
    };
    connect(m_triggerApplyBtn, &QPushButton::clicked, this, applyTrigger);

    // Mode-switch hotkey — save and re-register
    auto applyModeSwitch = [this]() {
        appConfig().settings.modeSwitchHotkey =
            m_modeSwitchEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoiceInputController::instance()) {
            ctrl->reregisterModeSwitchHotkey();
        }
        STATUSBAR_INFO("{}", tr("Mode switch shortcut applied"));
    };
    connect(m_modeSwitchApplyBtn, &QPushButton::clicked, this, applyModeSwitch);
}

void ShortcutSettingsWidget::updateActiveModeDisplay()
{
    const QString activeMode =
        QString::fromStdString(appConfig().settings.activeMode);
    const int idx = m_activeModeCombo->findData(activeMode);
    if (idx >= 0) {
        m_activeModeCombo->setCurrentIndex(idx);
    }
}

void ShortcutSettingsWidget::refreshFromConfig()
{
    {
        const QSignalBlocker b1(m_triggerEdit);
        const QSignalBlocker b2(m_modeSwitchEdit);
        m_triggerEdit->setKeySequence(QKeySequence(
            QString::fromStdString(appConfig().settings.triggerHotkey)));
        m_modeSwitchEdit->setKeySequence(QKeySequence(
            QString::fromStdString(appConfig().settings.modeSwitchHotkey)));
    }
    updateActiveModeDisplay();
}

} // namespace talkinput
