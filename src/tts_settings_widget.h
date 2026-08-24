#pragma once

#include <QCoro/QCoroTask>

#include <QWidget>
#include <QByteArray>
#include <memory>

class QComboBox;
class QEvent;
class QLabel;
class QAudioOutput;
class QMediaPlayer;
class QPushButton;
class QTextEdit;
class QTemporaryFile;

namespace Ui
{
class TtsSettingsWidget;
}

namespace talkinput
{

/// TTS service configuration ("TTS").
class TtsSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit TtsSettingsWidget(QWidget *parent = nullptr);
    ~TtsSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void onTtsProviderChanged(int index);
    void updateTtsWidgetStates();
    void refreshTtsModelStatus();
    void synthesizePreview();
    void playPreview();
    void savePreview();
    void onOpenTtsModelUrl();
    void onImportTtsModel();

    QCoro::Task<void> downloadTtsModel();

    std::unique_ptr<Ui::TtsSettingsWidget> m_ui;
    QAudioOutput *m_audioOutput = nullptr;
    QMediaPlayer *m_mediaPlayer = nullptr;
    std::unique_ptr<QTemporaryFile> m_previewFile;
    QByteArray m_previewPcm;
};

} // namespace talkinput
