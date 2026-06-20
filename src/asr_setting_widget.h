#pragma once

#include "json_utils.h"

#include <QNetworkAccessManager>
#include <QQueue>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>

class QComboBox;
class QEvent;
class QFile;
class QLineEdit;
class QNetworkReply;

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

    void loadActiveAsrPreset();
    void ensureAsrModelReady(const QString &providerId,
                             std::function<void()> onReady);
    void loadInstalledAsrModel(const QString &providerId);

    bool enqueueDownload(const QString &modelPointer, QString *errorMessage);
    void startNextDownload();
    void onDownloadFinished();
    void downloadFinishReady();
    void downloadFail();

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;

    bool m_isDownloading = false;
    std::function<void()> m_onDownloadReady;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    std::unique_ptr<QFile> m_downloadFile;
    QQueue<QString> m_pendingDownloads;
    QString m_activeDownloadPointer;
    QString m_activeArchivePath;
    QString m_activeTempPath;
};

} // namespace talkinput
