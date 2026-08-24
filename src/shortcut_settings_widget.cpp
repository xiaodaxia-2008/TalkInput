#include "shortcut_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "ui_shortcut_settings_widget.h"
#include "voice_input_controller.h"

#include <QEvent>
#include <QKeySequenceEdit>
#include <QPushButton>
#include <QSignalBlocker>

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
    m_ui = std::make_unique<Ui::ShortcutSettingsWidget>();
    m_ui->setupUi(this);
}

void ShortcutSettingsWidget::retranslate()
{
    m_ui->group->setTitle(tr("Shortcuts"));
    m_ui->triggerLabel->setText(tr("Global Input Method Trigger"));
    m_ui->modeSwitchLabel->setText(tr("Voice Input Mode"));
    m_ui->triggerLabel->setToolTip(
        tr("Global hotkey to trigger the current active mode"));
    m_ui->modeSwitchLabel->setToolTip(
        tr("Global hotkey to cycle the active pipeline mode"));
    m_ui->triggerApplyBtn->setToolTip(tr("Apply shortcut"));
    m_ui->modeSwitchApplyBtn->setToolTip(tr("Apply shortcut"));
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
    connect(m_ui->triggerApplyBtn, &QPushButton::clicked, this, [this]() {
        appConfig().settings.triggerHotkey =
            m_ui->triggerEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoiceInputController::instance()) {
            ctrl->reregisterTriggerHotkey();
        }
        STATUSBAR_INFO("{}", tr("Trigger shortcut applied"));
    });
    connect(m_ui->modeSwitchApplyBtn, &QPushButton::clicked, this,
            [this]() {
                appConfig().settings.modeSwitchHotkey =
                    m_ui->modeSwitchEdit->keySequence().toString().toStdString();
                markConfigDirty();
                if (auto *ctrl = VoiceInputController::instance()) {
                    ctrl->reregisterModeSwitchHotkey();
                }
                STATUSBAR_INFO("{}", tr("Mode switch shortcut applied"));
            });
}

void ShortcutSettingsWidget::refreshFromConfig()
{
    const QSignalBlocker b1(m_ui->triggerEdit);
    const QSignalBlocker b2(m_ui->modeSwitchEdit);
    m_ui->triggerEdit->setKeySequence(
        QKeySequence(QString::fromStdString(appConfig().settings.triggerHotkey)));
    m_ui->modeSwitchEdit->setKeySequence(
        QKeySequence(QString::fromStdString(appConfig().settings.modeSwitchHotkey)));
}

} // namespace talkinput
