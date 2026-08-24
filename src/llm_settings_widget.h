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

namespace Ui
{
class LlmSettingsWidget;
}

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

    std::unique_ptr<Ui::LlmSettingsWidget> m_ui;
    QNetworkAccessManager *m_network = nullptr;
};

} // namespace talkinput
