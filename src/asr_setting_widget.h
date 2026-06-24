#pragma once

#include "app_config.h"

#include <QCoro/QCoroTask>

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

class AsrSettingWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AsrSettingWidget(QWidget *parent = nullptr);
    ~AsrSettingWidget() override;
    void updateUiFromConfig();
    void updateActiveModeDisplay();

protected:
    void changeEvent(QEvent *event) override;

private:
    void onLlmProviderChanged(int index);
    void onOcrProviderChanged(int index);
    void onUseAsrModel();
    void onOpenModelUrl();
    void onImportModel();
    void onEditHotwords();
    void onEditPrompt();
    void applyLlmProviderToUi(const LlmPreset &provider);
    void refreshPromptLabel();

    void initLlmProviders();
    void initLlmPrompt();
    void initOcrProvider();
    void initAsrModel();
    void initIcons();
    void initShortcuts();
    void initActiveMode();

    QCoro::Task<void> useAsrModel(const QString &providerId);
    void loadInstalledAsrModel(const QString &providerId);
    void refreshAsrModelCombo();

    QCoro::Task<bool> downloadAsrModel(const QString &providerId);

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;
};

} // namespace talkinput
