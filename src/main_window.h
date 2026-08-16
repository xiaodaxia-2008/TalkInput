#pragma once

#include "recognition_history.h"
#include "theme.h"
#include "voice_input_controller.h"

#include <QCloseEvent>
#include <QMainWindow>
#include <QTranslator>
#include <memory>

class QSystemTrayIcon;
class QTreeWidgetItem;

namespace Ui
{
class MainWindow;
}

namespace talkinput
{

enum class SettingsPage : int
{
    RecognitionModel = 0,
    RecognitionBehavior,
    Ocr,
    Llm,
    Tts,
    ApiServer,
    Shortcut,
    Appearance,
    History,
    Log,
    General,
};

class ApiServerSettingsWidget;
class AppearanceSettingsWidget;
class GeneralSettingsWidget;
class HistoryWidget;
class LlmSettingsWidget;
class LogPanel;
class OcrSettingsWidget;
class RecognitionBehaviorWidget;
class RecognitionModelWidget;
class ShortcutSettingsWidget;
class TtsSettingsWidget;

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
    void setupSettingsPages();
    void setupTrayIcon();
    void setupNavTree();
    void retranslateNav();
    void refreshNavIcons();
    void onNavItemClicked(QTreeWidgetItem *item, int column);
    void restoreNavSelection();
    void refreshAllSettingsPages();
    void updateControls(bool listening);

    void onThemeChanged(ThemeMode mode);
    void onLanguageChanged(const QString &language);

    void onToggleSpeechRecognition();
    void onRecognizeAudioFile();
    void onShowMainWindow();
    void onQuitApplication();
    void onResetSettings();
    void onShowAboutDialog();
    void onOpenDataDirectory();

    std::unique_ptr<Ui::MainWindow> m_ui;
    RecognitionModelWidget *m_recognitionModelWidget = nullptr;
    RecognitionBehaviorWidget *m_recognitionBehaviorWidget = nullptr;
    OcrSettingsWidget *m_ocrSettingsWidget = nullptr;
    LlmSettingsWidget *m_llmSettingsWidget = nullptr;
    TtsSettingsWidget *m_ttsSettingsWidget = nullptr;
    ApiServerSettingsWidget *m_apiServerSettingsWidget = nullptr;
    ShortcutSettingsWidget *m_shortcutSettingsWidget = nullptr;
    AppearanceSettingsWidget *m_appearanceSettingsWidget = nullptr;
    GeneralSettingsWidget *m_generalSettingsWidget = nullptr;
    HistoryWidget *m_historyWidget = nullptr;
    LogPanel *m_logPanel = nullptr;
    VoiceInputController *m_voiceInputController = nullptr;
    RecognitionHistory m_history;

    QSystemTrayIcon *m_trayIcon = nullptr;
    QTranslator *m_appTranslator = nullptr;
    QTranslator *m_qtTranslator = nullptr;

    QTreeWidgetItem *m_activeNavItem = nullptr;
    QVector<QTreeWidgetItem *> m_navItems;
    QVector<QTreeWidgetItem *> m_sectionItems;

    bool m_dark = false;
    bool m_forceQuit = false;
};

} // namespace talkinput
