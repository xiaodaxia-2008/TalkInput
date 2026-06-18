#pragma once

#include <QUrl>
#include <QVector>
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

    struct ModeDef
    {
        QString label;
        QString primaryDirName; // modelDirName of primary model
        QString backupDirName; // modelDirName of fallback model (empty if none)
        bool isStreaming = false;
    };

    void onModeChanged(int index);
    void refreshStatus();
    int findModelRow(const QString &modelDirName) const;
    void onDownloadCurrent();
    void onDeleteCurrent();
    void activateModel(int modelRow);
    void onUseArchive();
    void onOpenDir();
    void onEditHotwords();
    void onDownloadFinished();

    void ensurePunctuationModel();
    bool isInstalled(int row) const;
    int currentPrimaryModelRow() const;
    static QString punctuationModelName();

    QVector<ModelInfo> m_models;
    QVector<ModeDef> m_modes;

    // UI
    QComboBox *m_modeCombo = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_dlBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    // Download
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
