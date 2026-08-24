#include "shortcut_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "voice_input_controller.h"

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
    m_triggerLabel->setText(tr("Global Speech Recognition Trigger"));
    m_modeSwitchLabel->setText(tr("Speech Recognition Mode Switch"));
    m_triggerApplyBtn->setToolTip(tr("Apply shortcut"));
    m_modeSwitchApplyBtn->setToolTip(tr("Apply shortcut"));
}

void ShortcutSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
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
}

} // namespace talkinput
