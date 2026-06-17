#pragma once

#include <QWidget>
#include <QUrl>
#include <QVector>
#include <memory>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QPushButton;
class QTableWidget;

namespace talkinput {

class SettingWidget final : public QWidget {
  Q_OBJECT

public:
  explicit SettingWidget(QWidget *parent = nullptr);
  ~SettingWidget() override;

signals:
  void modelSelected(const QString &modelDirectory,
                     const QString &modelName);
  void statusMessage(const QString &message);

private:
  struct ModelInfo {
    QString name;
    QString type;
    QString modelDirName;
    QUrl archiveUrl;
    qint64 modelSize = 0;
    int paramCount = 0;
    bool streamingSupport = false;

  };

  void populateTable();
  void refreshStatus();
  void onUse(int row);
  void onDownload(int row);
  void onDelete(int row);
  void onUseArchive();
  void onOpenDir();
  void onDownloadFinished();

  void applyIcon(QPushButton *btn, const QString &svgPath, int size);

  QTableWidget *m_table = nullptr;
  QVector<ModelInfo> m_models;

  QNetworkAccessManager *m_networkManager = nullptr;
  QNetworkReply *m_activeDownloadReply = nullptr;
  std::unique_ptr<QFile> m_activeDownloadFile;
  QString m_activeDownloadPath;
  QString m_activeDownloadTempPath;
  int m_downloadTargetRow = -1;
};

} // namespace talkinput
