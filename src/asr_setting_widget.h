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
    void refreshAsrStatus();
    void onUseAsrModel();
    void onEditHotwords();
    void onEditPrompt();

    // Init helpers
    void initLlmProviders();
    void initLlmPrompt();
    void initOcrProvider();
    void initLlmChecks();
    void initAsrModel();
    void initIcons();

    // Download helpers
    void startModelDownload(const QString &modelPointer);
    void onModelDownloadFinished();
    QString currentAsrPresetPath() const;

    std::unique_ptr<Ui::AsrSettingWidget> m_ui;

    // Download state
    QNetworkAccessManager *m_downloadManager = nullptr;
    QNetworkReply *m_downloadReply = nullptr;
    std::unique_ptr<QFile> m_downloadFile;
    QString m_downloadArchivePath;
    QString m_downloadTempPath;
    QString m_downloadingModelPath;
    QQueue<QString> m_downloadQueue;
};

} // namespace talkinput
