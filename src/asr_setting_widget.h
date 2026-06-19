#pragma once

#include "json_utils.h"

#include <QQueue>
#include <QString>
#include <QWidget>
#include <memory>

class QComboBox;
class QEvent;
class QFile;
class QLineEdit;
class QNetworkAccessManager;
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

protected:
    void changeEvent(QEvent *event) override;

signals:
    void hotwordsChanged();

private:
    void onLlmProviderChanged(int index);
    void onOcrProviderChanged(int index);
    void onAsrModelChanged(int index);
    void refreshStatus();
    void onUseCurrent();
    void onEditHotwords();
    void onEditPrompt();

    // Init helpers
    void initLlmProviders();
    void initPrompt();
    void initOcrProvider();
    void initLlmChecks();
    void initAsrModel();
    void initIcons();

    // Download helpers
    void startModelDownload(const QString &modelPointer);
    void onDownloadFinished();
    bool isInstalled(const nlohmann::json &model) const;
    QString currentModelPointer() const;

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;

    // Download state
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_activeDownloadReply = nullptr;
    std::unique_ptr<QFile> m_activeDownloadFile;
    QString m_activeDownloadPath;
    QString m_activeDownloadTempPath;
    QString m_downloadTargetPointer;
    QQueue<QString> m_downloadQueue;

    // Active ASR model — tracks which preset is currently loaded
    QString m_activeAsrId;
};

} // namespace talkinput
