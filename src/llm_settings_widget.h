#pragma once

#include "app_config.h"

#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;

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
    void refreshPromptLabel();
    void onEditPrompt();

    QGroupBox *m_group = nullptr;
    QLabel *m_providerFormLabel = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QLabel *m_endpointFormLabel = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLabel *m_llmModelFormLabel = nullptr;
    QComboBox *m_llmModelCombo = nullptr;
    QLabel *m_apiKeyFormLabel = nullptr;
    QLineEdit *m_apiKeyEdit = nullptr;
    QLabel *m_promptFormLabel = nullptr;
    QLabel *m_promptLabel = nullptr;
    QPushButton *m_promptEditButton = nullptr;
};

} // namespace talkinput
