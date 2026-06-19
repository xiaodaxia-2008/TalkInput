#pragma once

#include "json_utils.h"

#include <QQueue>
#include <QString>
#include <QWidget>
#include <memory>

class QComboBox;
class QFile;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTimer;

namespace talkinput
{

class AsrSettingWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit AsrSettingWidget(QWidget *parent = nullptr);
    ~AsrSettingWidget() override;

signals:
    void modelSelected(const QString &modelDirectory, const QString &modelName,
                       const QString &modelType);
    void statusMessage(const QString &message);
    void hotwordsChanged();

private:
    void onModelChanged(int index);
    void refreshStatus();
    void onDownloadCurrent();
    void onDeleteCurrent();
    void onUseCurrent();
    void activateModel(const QString &modelPointer);
    void onUseArchive();
    void onOpenDir();
    void onEditHotwords();
    void onDownloadFinished();

    void startModelDownload(const QString &modelPointer);
    bool isInstalled(const nlohmann::json &model) const;
    QString currentModelPointer() const;

    // UI
    QComboBox *m_modelCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_dlBtn = nullptr;
    QPushButton *m_delBtn = nullptr;
    QPushButton *m_useBtn = nullptr;

    // Download
    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_activeDownloadReply = nullptr;
    std::unique_ptr<QFile> m_activeDownloadFile;
    QString m_activeDownloadPath;
    QString m_activeDownloadTempPath;
    QString m_downloadTargetPointer;
    QQueue<QString> m_downloadQueue;
};

} // namespace talkinput
