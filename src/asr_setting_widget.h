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
    QString asrPresetPathById(const QString &id) const;
    nlohmann::json asrPresetAt(int index) const;
    nlohmann::json currentAsrPreset() const;
    QString currentAsrPresetPath() const;
    void loadActiveAsrPreset();
    void loadAsrPreset(const QString &modelPointer);
    bool enqueueAsrModelDownloads(const QString &modelPointer,
                                  QString *errorMessage);
    void startNextAsrModelDownload();
    void onAsrModelDownloadFinished();
    void clearAsrModelDownload();

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;
    std::unique_ptr<QNetworkAccessManager> m_asrDownloadNetwork;
    QNetworkReply *m_asrDownloadReply = nullptr;
    std::unique_ptr<QFile> m_asrDownloadFile;
    QQueue<QString> m_pendingAsrDownloadPointers;
    QString m_requestedAsrModelPointer;
    QString m_activeAsrDownloadPointer;
    QString m_activeAsrArchivePath;
    QString m_activeAsrTempPath;
};

} // namespace talkinput
