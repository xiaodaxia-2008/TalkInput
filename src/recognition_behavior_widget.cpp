#include "recognition_behavior_widget.h"
#include "app_config.h"
#include "ui_recognition_behavior_widget.h"

#include <QCheckBox>
#include <QEvent>
#include <QSignalBlocker>

namespace talkinput
{

RecognitionBehaviorWidget::RecognitionBehaviorWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

RecognitionBehaviorWidget::~RecognitionBehaviorWidget() = default;

void RecognitionBehaviorWidget::buildUi()
{
    m_ui = std::make_unique<Ui::RecognitionBehaviorWidget>();
    m_ui->setupUi(this);
    connect(m_ui->useClipboardCheck, &QCheckBox::toggled, this, [](bool checked) {
        appConfig().settings.useClipboard = checked;
        markConfigDirty();
    });
    connect(m_ui->copyToClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.copyToClipboard = checked;
                markConfigDirty();
            });
    connect(m_ui->restoreClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.restoreClipboard = checked;
                markConfigDirty();
            });
    connect(m_ui->saveOcrScreenshotCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.saveOcrScreenshot = checked;
                markConfigDirty();
            });
    connect(m_ui->saveAsrAudioCheck, &QCheckBox::toggled, this, [](bool checked) {
        appConfig().settings.saveAsrAudio = checked;
        markConfigDirty();
    });
}

void RecognitionBehaviorWidget::retranslate()
{
    m_ui->useClipboardCheck->setText(tr("Use Clipboard"));
    m_ui->useClipboardCheck->setToolTip(tr("Use clipboard to paste text"));
    m_ui->copyToClipboardCheck->setText(tr("Copy to Clipboard"));
    m_ui->copyToClipboardCheck->setToolTip(tr("Copy result to clipboard"));
    m_ui->restoreClipboardCheck->setText(tr("Restore Clipboard"));
    m_ui->restoreClipboardCheck->setToolTip(
        tr("Restore original clipboard content after paste"));
    m_ui->saveOcrScreenshotCheck->setText(tr("Save Screenshot"));
    m_ui->saveOcrScreenshotCheck->setToolTip(
        tr("Save OCR context screenshot locally for debugging"));
    m_ui->saveAsrAudioCheck->setText(tr("Save Audio"));
    m_ui->saveAsrAudioCheck->setToolTip(
        tr("Save recorded audio to disk for debugging"));
}

void RecognitionBehaviorWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void RecognitionBehaviorWidget::refreshFromConfig()
{
    const QSignalBlocker b1(m_ui->useClipboardCheck);
    const QSignalBlocker b2(m_ui->copyToClipboardCheck);
    const QSignalBlocker b3(m_ui->restoreClipboardCheck);
    const QSignalBlocker b4(m_ui->saveOcrScreenshotCheck);
    const QSignalBlocker b5(m_ui->saveAsrAudioCheck);
    m_ui->useClipboardCheck->setChecked(appConfig().settings.useClipboard);
    m_ui->copyToClipboardCheck->setChecked(appConfig().settings.copyToClipboard);
    m_ui->restoreClipboardCheck->setChecked(appConfig().settings.restoreClipboard);
    m_ui->saveOcrScreenshotCheck->setChecked(
        appConfig().settings.saveOcrScreenshot);
    m_ui->saveAsrAudioCheck->setChecked(appConfig().settings.saveAsrAudio);
}

} // namespace talkinput
