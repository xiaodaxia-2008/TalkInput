#pragma once

#include <QUrl>
#include <QVector>
#include <QWidget>
#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;
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
    void modelSelected(const QString &modelDirectory, const QString &modelName);
    void statusMessage(const QString &message);
    void punctuationModelReady();
    void hotwordsChanged();

private:
    struct ModelInfo
    {
        QString name;
        QString type;
        QString languages;
        QString modelDirName;
        QUrl archiveUrl;
        qint64 modelSize = 0;
        int paramCount = 0;
        bool streamingSupport = false;
        bool isPunctuationModel = false;
    };

    void populateTable();
    void refreshStatus();
    void onUse(int row);
    void onDownload(int row);
    void onDelete(int row);
    void onUseArchive();
    void onOpenDir();
    void onEditHotwords();
    void onDownloadFinished();

    void applyIcon(QPushButton *btn, const QString &svgPath, int size);
    void ensurePunctuationModel();
    bool isInstalled(int row) const;
    static QString punctuationModelName();

    QTableWidget *m_table = nullptr;
    QVector<ModelInfo> m_models;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_activeDownloadReply = nullptr;
    std::unique_ptr<QFile> m_activeDownloadFile;
    QString m_activeDownloadPath;
    QString m_activeDownloadTempPath;
    int m_downloadTargetRow = -1;
    int m_punctuationRow = -1;
    QTimer *m_startupTimer = nullptr;
};

} // namespace talkinput
