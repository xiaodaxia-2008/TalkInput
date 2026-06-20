#pragma once

#include "json_utils.h"

#include <QPointer>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>

class QComboBox;
class QEvent;
class QLineEdit;
class QObject;

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

protected:
    void changeEvent(QEvent *event) override;

private:
    void onLlmProviderChanged(int index);
    void onOcrProviderChanged(int index);
    void onAsrModelChanged(int index);
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

    void loadActiveAsrPreset();
    void ensureAsrModelReady(const QString &providerId,
                             std::function<void()> onReady);
    void loadInstalledAsrModel(const QString &providerId);

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;
    QPointer<QObject> m_asrModelLoadChain;
};

} // namespace talkinput
