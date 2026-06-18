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
    void modelSelected(const QString &modelDirectory, const QString &modelName,
                       const QString &modelType);
    void statusMessage(const QString &message);
    void punctuationModelReady(const QString &punctuationDir);
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
        QString postPunctuationDirName; // dir name of punctuation model partner
    };

    void onModelChanged(int index);
    void refreshStatus();
    void onDownloadCurrent();
    void onDeleteCurrent();
    void onUseCurrent();
    void activateModel(int modelRow);
    void onUseArchive();
    void onOpenDir();
    void onEditHotwords();
    void onDownloadFinished();

    void autoLoadPunctuationModel(int modelRow);
    bool isInstalled(int row) const;
    int currentModelRow() const;

    QVector<ModelInfo> m_models;

    // Maps combo index → m_models index for ASR (non-punctuation) models
    QVector<int> m_asrModelIndices;

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
    int m_downloadTargetRow = -1;
};

} // namespace talkinput
