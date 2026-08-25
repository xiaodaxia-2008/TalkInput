#include "shortcut_settings_widget.h"
#include "app_config.h"
#include "logging.h"
#include "ui_shortcut_settings_widget.h"
#include "voice_pipeline_controller.h"

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
    m_ui->retranslateUi(this);
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
        if (auto *ctrl = VoicePipelineController::instance()) {
            ctrl->reregisterTriggerHotkey();
        }
        STATUSBAR_INFO("{}", tr("Trigger shortcut applied"));
    });
    connect(m_ui->modeSwitchApplyBtn, &QPushButton::clicked, this, [this]() {
        appConfig().settings.modeSwitchHotkey =
            m_ui->modeSwitchEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoicePipelineController::instance()) {
            ctrl->reregisterModeSwitchHotkey();
        }
        STATUSBAR_INFO("{}", tr("Mode switch shortcut applied"));
    });
}

void ShortcutSettingsWidget::refreshFromConfig()
{
    const QSignalBlocker b1(m_ui->triggerEdit);
    const QSignalBlocker b2(m_ui->modeSwitchEdit);
    m_ui->triggerEdit->setKeySequence(QKeySequence(
        QString::fromStdString(appConfig().settings.triggerHotkey)));
    m_ui->modeSwitchEdit->setKeySequence(QKeySequence(
        QString::fromStdString(appConfig().settings.modeSwitchHotkey)));
}

} // namespace talkinput
