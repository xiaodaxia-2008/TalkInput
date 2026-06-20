#pragma once

#include "json_utils.h"

#include <QString>
#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QLineEdit;

namespace Ui
{
class AsrSettingWidget;
}

namespace talkinput
{

class ModelDownloadManager;

class AsrSettingWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AsrSettingWidget(QWidget *parent = nullptr);
    ~AsrSettingWidget() override;
    void updateUiFromConfig();

protected:
    void changeEvent(QEvent *event) override;

private:
    void onLlmProviderChanged(int index);
    void onOcrProviderChanged(int index);
    void onAsrModelChanged(int index);
    void refreshAsrStatus();
    void onUseAsrModel();
    void onEditHotwords();
    void onEditPrompt();
    void applyLlmProviderToUi(const nlohmann::json &provider);
    void refreshPromptLabel();

    // Init helpers
    void initLlmProviders();
    void initLlmPrompt();
    void initOcrProvider();
    void initLlmChecks();
    void initAsrModel();
    void initIcons();

    void onDownloadFinished(const QString &modelPointer);
    ModelDownloadManager &ensureModelDownloadManager();
    nlohmann::json asrPresetAt(int index) const;
    nlohmann::json currentAsrPreset() const;
    QString currentAsrPresetPath() const;
    void loadActiveAsrPreset();
    void loadAsrPreset(const nlohmann::json &preset);

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;
    std::unique_ptr<ModelDownloadManager> m_downloadManager;
};

} // namespace talkinput
