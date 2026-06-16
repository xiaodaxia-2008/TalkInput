#pragma once

#include "recognition_history.h"
#include "speech_recognizer.h"

#include <QAudioFormat>
#include <QFuture>
#include <QFutureWatcher>
#include <QMainWindow>
#include <memory>

class QAudioSource;
class QDialog;
class QIODevice;

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

private:
  void setupUi();
  void toggleListening();
  void startListening();
  void stopListening();
  void readAudio();
  void updateControls(bool listening);
  void setRecognitionModel(const QString &modelDirectory,
                            const QString &modelName = QString());
  void showLoadingDialog(const QString &message);
  void hideLoadingDialog();
  void loadModelAsync(const SpeechRecognizer::Config &config);
  void onModelLoaded();
  void onResult(const QString &text, bool isFinal);
  void refreshHistory();
  void copyEntry(int row);
  void deleteEntry(int row);

  std::unique_ptr<Ui::MainWindow> m_ui;
  SettingWidget *m_settingWidget = nullptr;
  SpeechRecognizer m_recognizer;
  RecognitionHistory m_history;

  QString m_currentModelDirectory;
  QString m_currentModelName;
  bool m_loadingModel = false;

  std::unique_ptr<QAudioSource> m_audioSource;
  QIODevice *m_audioDevice = nullptr;
  QAudioFormat m_audioFormat;

  QDialog *m_loadingDialog = nullptr;
  QFuture<bool> m_modelLoadFuture;
  QFutureWatcher<bool> m_modelLoadWatcher;
};

} // namespace talkinput
