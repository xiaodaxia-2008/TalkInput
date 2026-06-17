#pragma once

#include "asr_service.h"
#include "recognition_history.h"
#include "voice_input_controller.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QTranslator>
#include <memory>

class QLabel;
class QSystemTrayIcon;
class QThread;

namespace Ui {
class MainWindow;
}

namespace talkinput {

class SettingWidget;

class MainWindow final : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  void setupUi();
  void setupTrayIcon();
  void startListening();
  void stopListening();
  void updateControls(bool listening);
  void setRecognitionModel(const QString &modelDirectory,
                            const QString &modelName = QString());
  void onResult(const QString &text, bool isFinal);
  void refreshHistory();
  void editEntry(int row);
  void copyEntry(int row);
  void deleteEntry(int row);
  void onRecognizeFile();
  void retranslateUi();
  void doSwitchLanguage(const QString &lang);

  std::unique_ptr<Ui::MainWindow> m_ui;
  SettingWidget *m_settingWidget = nullptr;
  VoiceInputController *m_voiceInput = nullptr;
  AsrService *m_asrService = nullptr;
  QThread *m_asrThread = nullptr;
  RecognitionHistory m_history;

  QString m_currentModelDirectory;
  QString m_currentModelName;
  QString m_currentLanguage;

  QSystemTrayIcon *m_trayIcon = nullptr;
  QLabel *m_realtimeLabel = nullptr;
  QTranslator *m_appTranslator = nullptr;
  QTranslator *m_qtTranslator = nullptr;

  // Menus created dynamically (needed for retranslation)
  QMenu *m_prefMenu = nullptr;
  QMenu *m_langMenu = nullptr;
  QAction *m_zhAction = nullptr;
  QAction *m_enAction = nullptr;
  QMenu *m_helpMenu = nullptr;
};

} // namespace talkinput
