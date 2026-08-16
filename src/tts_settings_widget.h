#pragma once

#include <QCoro/QCoroTask>

#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QPushButton;

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
    void onOpenTtsModelUrl();
    void onImportTtsModel();

    QCoro::Task<void> downloadTtsModel();

    QGroupBox *m_group = nullptr;
    QLabel *m_providerFormLabel = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QLabel *m_voiceFormLabel = nullptr;
    QComboBox *m_voiceCombo = nullptr;
    QLabel *m_modelFormLabel = nullptr;
    QLabel *m_modelStatusLabel = nullptr;
    QPushButton *m_browserButton = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_downloadButton = nullptr;
};

} // namespace talkinput
