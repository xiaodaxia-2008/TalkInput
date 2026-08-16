#pragma once

#include <QWidget>
#include <memory>

class QCheckBox;
class QEvent;
class QGroupBox;
class QLabel;

namespace talkinput
{

/// Speech recognition behavior checkboxes ("识别行为").
class RecognitionBehaviorWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit RecognitionBehaviorWidget(QWidget *parent = nullptr);
    ~RecognitionBehaviorWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();

    QGroupBox *m_group = nullptr;
    QLabel *m_hintLabel = nullptr;
    QCheckBox *m_useClipboardCheck = nullptr;
    QCheckBox *m_copyToClipboardCheck = nullptr;
    QCheckBox *m_restoreClipboardCheck = nullptr;
    QCheckBox *m_saveOcrScreenshotCheck = nullptr;
    QCheckBox *m_saveAsrAudioCheck = nullptr;
};

} // namespace talkinput
