#pragma once

#include "app_config.h"

#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QLabel;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
class QTextEdit;

namespace talkinput
{

/// LLM service configuration ("LLM").
class LlmSettingsWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit LlmSettingsWidget(QWidget *parent = nullptr);
    ~LlmSettingsWidget() override;

    /// Re-reads the current config and refreshes every control.
    void refreshFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void buildUi();
    void retranslate();
    void onLlmProviderChanged(int index);
    void applyLlmProviderToUi(const LlmPreset &provider);
    void onPromptChanged();
    void refreshModels();

    QLabel *m_providerFormLabel = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QLabel *m_endpointFormLabel = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLabel *m_llmModelFormLabel = nullptr;
    QComboBox *m_llmModelCombo = nullptr;
    QLabel *m_apiKeyFormLabel = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLabel *m_promptFormLabel = nullptr;
    QTextEdit *m_promptEdit = nullptr;
    QPushButton *m_refreshModelsButton = nullptr;
    QNetworkAccessManager *m_network = nullptr;
};

} // namespace talkinput
