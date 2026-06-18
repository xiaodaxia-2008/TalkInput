#pragma once

#include "asr_service.h"
#include "recognition_history.h"
#include "voice_input_controller.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QTranslator>
#include <memory>

class QAction;
class QMenu;
class QSystemTrayIcon;
class QThread;
class QToolBar;

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
    void retranslateUi();
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

    // Menus created dynamically (needed for retranslation)
    QToolBar *m_recognitionToolBar = nullptr;
    QAction *m_startAction = nullptr;
    QAction *m_fileAction = nullptr;
    QAction *m_exitAction = nullptr;
    QMenu *m_prefMenu = nullptr;
    QMenu *m_langMenu = nullptr;
    QAction *m_zhAction = nullptr;
    QAction *m_enAction = nullptr;
    QAction *m_startHiddenAction = nullptr;
    QAction *m_resetSettingsAction = nullptr;
    QMenu *m_helpMenu = nullptr;
    bool m_forceQuit = false;
};

} // namespace talkinput
