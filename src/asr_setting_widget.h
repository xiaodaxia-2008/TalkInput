#pragma once

#include "json_utils.h"

#include <QCoro/QCoroTask>

#include <QNetworkAccessManager>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>

class QComboBox;
class QEvent;
class QFile;
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

    void initLlmProviders();
    void initLlmPrompt();
    void initOcrProvider();
    void initLlmChecks();
    void initAsrModel();
    void initIcons();
    void initShortcuts();

    void loadActiveAsrPreset();
    void ensureAsrModelReady(const QString &providerId,
                             std::function<void()> onReady);
    void loadInstalledAsrModel(const QString &providerId);

    QCoro::Task<void> downloadModels(QString providerId);
    void downloadCleanupDone();
    void downloadCleanupFail();

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;

    bool m_isDownloading = false;
    std::function<void()> m_onDownloadReady;
    QNetworkAccessManager m_network;
    std::unique_ptr<QFile> m_downloadFile;
};

} // namespace talkinput
