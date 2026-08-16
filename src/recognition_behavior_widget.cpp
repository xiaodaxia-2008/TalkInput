#include "recognition_behavior_widget.h"
#include "app_config.h"

#include <QCheckBox>
#include <QEvent>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

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
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    m_group = new QGroupBox(content);
    auto *groupLayout = new QVBoxLayout(m_group);
    groupLayout->setContentsMargins(16, 20, 16, 14);
    groupLayout->setSpacing(10);

    m_hintLabel = new QLabel(m_group);
    m_hintLabel->setWordWrap(true);
    groupLayout->addWidget(m_hintLabel);

    m_useClipboardCheck = new QCheckBox(m_group);
    m_copyToClipboardCheck = new QCheckBox(m_group);
    m_restoreClipboardCheck = new QCheckBox(m_group);
    m_saveOcrScreenshotCheck = new QCheckBox(m_group);
    m_saveAsrAudioCheck = new QCheckBox(m_group);
    groupLayout->addWidget(m_useClipboardCheck);
    groupLayout->addWidget(m_copyToClipboardCheck);
    groupLayout->addWidget(m_restoreClipboardCheck);
    groupLayout->addWidget(m_saveOcrScreenshotCheck);
    groupLayout->addWidget(m_saveAsrAudioCheck);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

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
    m_group->setTitle(tr("Recognition Behavior"));
    m_hintLabel->setText(
        tr("These options control how recognition results are delivered."));
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
