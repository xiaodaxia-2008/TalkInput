#pragma once

#include "asr_service.h"
#include "recognition_history.h"
#include "voice_input_controller.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QTranslator>
#include <memory>

class QSystemTrayIcon;
class QThread;

namespace Ui
{
class MainWindow;
}

namespace talkinput
{

class AsrSettingWidget;
class HistoryWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void setupUi();
    void setupAsrSettingWidget();
    void setupTrayIcon();
    void startListening();
    void stopListening();
    void updateControls(bool listening);
    void setRecognitionModel(const QString &modelDirectory,
                             const QString &modelName = QString(),
                             const QString &modelType = QString());
    void onResult(const QString &text, bool isFinal);
    void onRecognizeFile();
    void quitApplication();
    void doSwitchLanguage(const QString &lang);
    void resetUserSettings();

    std::unique_ptr<Ui::MainWindow> m_ui;
    AsrSettingWidget *m_asrSettingWidget = nullptr;
    HistoryWidget *m_historyWidget = nullptr;
    VoiceInputController *m_voiceInput = nullptr;
    AsrService *m_asrService = nullptr;
    QThread *m_asrThread = nullptr;
    RecognitionHistory m_history;

    QString m_currentModelDirectory;
    QString m_currentModelName;
    QString m_currentModelType;
    QString m_currentLanguage;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;

    bool m_forceQuit = false;
};

} // namespace talkinput
