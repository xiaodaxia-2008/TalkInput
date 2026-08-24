#include "recognition_behavior_widget.h"
#include "app_config.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
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
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    m_useClipboardCheck = new QCheckBox(this);
    m_copyToClipboardCheck = new QCheckBox(this);
    m_restoreClipboardCheck = new QCheckBox(this);
    m_saveOcrScreenshotCheck = new QCheckBox(this);
    m_saveAsrAudioCheck = new QCheckBox(this);
    layout->addWidget(m_useClipboardCheck);
    layout->addWidget(m_copyToClipboardCheck);
    layout->addWidget(m_restoreClipboardCheck);
    layout->addWidget(m_saveOcrScreenshotCheck);
    layout->addWidget(m_saveAsrAudioCheck);
    layout->addStretch();

    connect(m_useClipboardCheck, &QCheckBox::toggled, this, [](bool checked) {
        appConfig().settings.useClipboard = checked;
        markConfigDirty();
    });
    connect(m_copyToClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.copyToClipboard = checked;
                markConfigDirty();
            });
    connect(m_restoreClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.restoreClipboard = checked;
                markConfigDirty();
            });
    connect(m_saveOcrScreenshotCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.saveOcrScreenshot = checked;
                markConfigDirty();
            });
    connect(m_saveAsrAudioCheck, &QCheckBox::toggled, this, [](bool checked) {
        appConfig().settings.saveAsrAudio = checked;
        markConfigDirty();
    });
}

void RecognitionBehaviorWidget::retranslate()
{
    m_useClipboardCheck->setText(tr("Use Clipboard"));
    m_useClipboardCheck->setToolTip(tr("Use clipboard to paste text"));
    m_copyToClipboardCheck->setText(tr("Copy to Clipboard"));
    m_copyToClipboardCheck->setToolTip(tr("Copy result to clipboard"));
    m_restoreClipboardCheck->setText(tr("Restore Clipboard"));
    m_restoreClipboardCheck->setToolTip(
        tr("Restore original clipboard content after paste"));
    m_saveOcrScreenshotCheck->setText(tr("Save Screenshot"));
    m_saveOcrScreenshotCheck->setToolTip(
        tr("Save OCR context screenshot locally for debugging"));
    m_saveAsrAudioCheck->setText(tr("Save Audio"));
    m_saveAsrAudioCheck->setToolTip(
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
    const QSignalBlocker b1(m_useClipboardCheck);
    const QSignalBlocker b2(m_copyToClipboardCheck);
    const QSignalBlocker b3(m_restoreClipboardCheck);
    const QSignalBlocker b4(m_saveOcrScreenshotCheck);
    const QSignalBlocker b5(m_saveAsrAudioCheck);
    m_useClipboardCheck->setChecked(appConfig().settings.useClipboard);
    m_copyToClipboardCheck->setChecked(appConfig().settings.copyToClipboard);
    m_restoreClipboardCheck->setChecked(appConfig().settings.restoreClipboard);
    m_saveOcrScreenshotCheck->setChecked(
        appConfig().settings.saveOcrScreenshot);
    m_saveAsrAudioCheck->setChecked(appConfig().settings.saveAsrAudio);
}

} // namespace talkinput
