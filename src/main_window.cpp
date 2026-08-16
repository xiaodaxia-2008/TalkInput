#include "main_window.h"
#include "api_server_settings_widget.h"
#include "app_config.h"
#include "appearance_settings_widget.h"
#include "audio_utils.h"
#include "general_settings_widget.h"
#include "history_widget.h"
#include "llm_settings_widget.h"
#include "log_panel.h"
#include "logging.h"
#include "ocr_settings_widget.h"
#include "recognition_behavior_widget.h"
#include "recognition_model_widget.h"
#include "shortcut_settings_widget.h"
#include "tts_settings_widget.h"
#include "ui_main_window.h"
#include "utils.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace talkinput
{

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ui(std::make_unique<Ui::MainWindow>())
{
    SPDLOG_DEBUG("MainWindow: constructor begin");
    installAppTranslations(currentAppLanguage(), this, m_appTranslator,
                           m_qtTranslator);
    setupUi();
    SPDLOG_DEBUG("MainWindow: constructor end");
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_forceQuit) {
        event->accept();
        return;
    }

    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::LanguageChange) {
        return;
    }

    m_ui->retranslateUi(this);
    retranslateNav();
    updateControls(m_voiceInputController &&
                   m_voiceInputController->isListening());
}

void MainWindow::setupUi()
{
    SPDLOG_DEBUG("setupUi: begin");
    m_ui->setupUi(this);
    installStatusBarLogger(statusBar());
    SPDLOG_DEBUG("setupUi: ui setup complete");

    m_dark = isDarkTheme(themeModeFromString(appConfig().settings.theme));

    // ── VoiceInputController (ASR + hotkey + overlay + LLM + text injection) ─
    SPDLOG_DEBUG("setupUi: creating VoiceInputController");
    m_voiceInputController = new VoiceInputController(this);

    // ── Navigation sidebar ─────────────────────────────────────────
    setupNavTree();

    // ── Settings pages ─────────────────────────────────────────────
    setupSettingsPages();

    // ── History page ───────────────────────────────────────────────
    SPDLOG_DEBUG("setupUi: creating HistoryWidget");
    m_historyWidget = new HistoryWidget(&m_history, m_ui->pageHistory);
    m_ui->pageHistoryLayout->addWidget(m_historyWidget);
    SPDLOG_DEBUG("setupUi: HistoryWidget added");

    // ── Log page ───────────────────────────────────────────────────
    m_logPanel = new LogPanel(m_ui->pageLog);
    m_ui->pageLogLayout->addWidget(m_logPanel);
    installLogPanelSink(m_logPanel->textEdit());

    connect(m_ui->actionStartRecognition, &QAction::triggered, this,
            &MainWindow::onToggleSpeechRecognition);

    connect(m_ui->actionRecognizeFile, &QAction::triggered, this,
            &MainWindow::onRecognizeAudioFile);

    SPDLOG_INFO("Starting ASR service");

    // resultChanged comes from VoiceInputController → onResult
    connect(m_voiceInputController, &VoiceInputController::finalTextCommitted,
            this, [this](const QString &text) {
                m_history.addEntry(text);
                if (m_historyWidget) {
                    m_historyWidget->refreshHistory();
                }
            });
    connect(m_voiceInputController, &VoiceInputController::listeningChanged,
            this, [this](bool listening) { updateControls(listening); });
    connect(m_voiceInputController, &VoiceInputController::modeChanged, this,
            [this](PipelineMode) {
                if (m_shortcutSettingsWidget) {
                    m_shortcutSettingsWidget->updateActiveModeDisplay();
                }
            });

    // ── System tray ────────────────────────────────────────────────
    SPDLOG_DEBUG("setupUi: setting up tray icon");
    setupTrayIcon();

    connect(m_appearanceSettingsWidget, &AppearanceSettingsWidget::themeChanged,
            this, &MainWindow::onThemeChanged);
    connect(m_appearanceSettingsWidget,
            &AppearanceSettingsWidget::languageChanged, this,
            &MainWindow::onLanguageChanged);

    connect(m_generalSettingsWidget,
            &GeneralSettingsWidget::resetSettingsRequested, this,
            &MainWindow::onResetSettings);
    connect(m_generalSettingsWidget,
            &GeneralSettingsWidget::openDataDirectoryRequested, this,
            &MainWindow::onOpenDataDirectory);
    connect(m_generalSettingsWidget, &GeneralSettingsWidget::aboutRequested,
            this, &MainWindow::onShowAboutDialog);
    connect(m_generalSettingsWidget, &GeneralSettingsWidget::exitRequested,
            this, &MainWindow::onQuitApplication);

    // Quit shortcut (menu bar was removed in favour of the sidebar).
    auto *quitShortcut = new QShortcut(QKeySequence::Quit, this);
    connect(quitShortcut, &QShortcut::activated, this,
            &MainWindow::onQuitApplication);

    // Re-apply the "follow system" theme when the OS switches color scheme.
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme) {
                if (themeModeFromString(appConfig().settings.theme) ==
                    ThemeMode::System)
                {
                    m_dark = applyTheme(ThemeMode::System);
                    refreshNavIcons();
                }
            });

    // Ensure the palette / icons match the configured theme.
    onThemeChanged(themeModeFromString(appConfig().settings.theme));

    SPDLOG_DEBUG("setupUi: end");
}

void MainWindow::setupSettingsPages()
{
    SPDLOG_DEBUG("setupSettingsPages: begin");

    m_recognitionModelWidget =
        new RecognitionModelWidget(m_ui->pageRecognitionModel);
    m_ui->recognitionModelContentLayout->addWidget(m_recognitionModelWidget);

    m_recognitionBehaviorWidget =
        new RecognitionBehaviorWidget(m_ui->pageRecognitionBehavior);
    m_ui->recognitionBehaviorContentLayout->addWidget(
        m_recognitionBehaviorWidget);

    m_ocrSettingsWidget = new OcrSettingsWidget(m_ui->pageOcr);
    m_ui->ocrContentLayout->addWidget(m_ocrSettingsWidget);

    m_llmSettingsWidget = new LlmSettingsWidget(m_ui->pageLlm);
    m_ui->llmContentLayout->addWidget(m_llmSettingsWidget);

    m_ttsSettingsWidget = new TtsSettingsWidget(m_ui->pageTts);
    m_ui->ttsContentLayout->addWidget(m_ttsSettingsWidget);

    m_apiServerSettingsWidget =
        new ApiServerSettingsWidget(m_ui->pageApiServer);
    m_ui->apiServerContentLayout->addWidget(m_apiServerSettingsWidget);

    m_shortcutSettingsWidget = new ShortcutSettingsWidget(m_ui->pageShortcut);
    m_ui->shortcutContentLayout->addWidget(m_shortcutSettingsWidget);

    m_appearanceSettingsWidget =
        new AppearanceSettingsWidget(m_ui->pageAppearance);
    m_ui->appearanceContentLayout->addWidget(m_appearanceSettingsWidget);

    m_generalSettingsWidget = new GeneralSettingsWidget(m_ui->pageGeneral);
    m_ui->generalContentLayout->addWidget(m_generalSettingsWidget);

    SPDLOG_DEBUG("setupSettingsPages: end");
}

void MainWindow::setupNavTree()
{
    SPDLOG_DEBUG("setupNavTree: begin");
    auto *tree = m_ui->navTree;

    const auto makeItem = [&](const QString &text, const QString &iconPath,
                              int page) {
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, text);
        item->setIcon(0, themedNavIcon(iconPath, m_dark));
        item->setData(0, Qt::UserRole, page);
        m_navItems.append(item);
        return item;
    };

    auto *speechItem =
        makeItem(tr("Speech Recognition"),
                 QStringLiteral(":/resources/icons/mic.svg"), -1);
    auto *modelItem = makeItem(
        tr("Model and Hot Words"), QStringLiteral(":/resources/icons/cpu.svg"),
        static_cast<int>(SettingsPage::RecognitionModel));
    auto *behaviorItem = makeItem(
        tr("Recognition Behavior"), QStringLiteral(":/resources/icons/zap.svg"),
        static_cast<int>(SettingsPage::RecognitionBehavior));
    speechItem->addChild(modelItem);
    speechItem->addChild(behaviorItem);

    auto *serviceItem = makeItem(
        tr("Services"), QStringLiteral(":/resources/icons/server.svg"), -1);
    auto *ocrItem =
        makeItem(tr("OCR"), QStringLiteral(":/resources/icons/camera.svg"),
                 static_cast<int>(SettingsPage::Ocr));
    auto *llmItem = makeItem(
        tr("LLM"), QStringLiteral(":/resources/icons/message-square.svg"),
        static_cast<int>(SettingsPage::Llm));
    auto *ttsItem =
        makeItem(tr("TTS"), QStringLiteral(":/resources/icons/volume-2.svg"),
                 static_cast<int>(SettingsPage::Tts));
    auto *apiServerItem =
        makeItem(tr("API Server"), QStringLiteral(":/resources/icons/link.svg"),
                 static_cast<int>(SettingsPage::ApiServer));
    serviceItem->addChild(ocrItem);
    serviceItem->addChild(llmItem);
    serviceItem->addChild(ttsItem);
    serviceItem->addChild(apiServerItem);

    makeItem(tr("Shortcuts"), QStringLiteral(":/resources/icons/keyboard.svg"),
             static_cast<int>(SettingsPage::Shortcut));
    makeItem(tr("Appearance"), QStringLiteral(":/resources/icons/palette.svg"),
             static_cast<int>(SettingsPage::Appearance));
    makeItem(tr("History"), QStringLiteral(":/resources/icons/clock.svg"),
             static_cast<int>(SettingsPage::History));
    makeItem(tr("Log"), QStringLiteral(":/resources/icons/terminal.svg"),
             static_cast<int>(SettingsPage::Log));
    makeItem(tr("General"), QStringLiteral(":/resources/icons/sliders.svg"),
             static_cast<int>(SettingsPage::General));

    speechItem->setExpanded(true);
    serviceItem->setExpanded(true);

    connect(tree, &QTreeWidget::itemClicked, this,
            &MainWindow::onNavItemClicked);

    m_activeNavItem = modelItem;
    tree->setCurrentItem(m_activeNavItem);
    m_ui->contentStack->setCurrentIndex(
        static_cast<int>(SettingsPage::RecognitionModel));
    SPDLOG_DEBUG("setupNavTree: end");
}

void MainWindow::retranslateNav()
{
    const QStringList labels = {tr("Speech Recognition"),
                                tr("Model and Hot Words"),
                                tr("Recognition Behavior"),
                                tr("Services"),
                                tr("OCR"),
                                tr("LLM"),
                                tr("TTS"),
                                tr("API Server"),
                                tr("Shortcuts"),
                                tr("Appearance"),
                                tr("History"),
                                tr("Log"),
                                tr("General")};
    for (int i = 0; i < m_navItems.size() && i < labels.size(); ++i) {
        m_navItems.at(i)->setText(0, labels.at(i));
    }
}

void MainWindow::refreshNavIcons()
{
    const QStringList iconPaths = {
        QStringLiteral(":/resources/icons/mic.svg"),
        QStringLiteral(":/resources/icons/cpu.svg"),
        QStringLiteral(":/resources/icons/zap.svg"),
        QStringLiteral(":/resources/icons/server.svg"),
        QStringLiteral(":/resources/icons/camera.svg"),
        QStringLiteral(":/resources/icons/message-square.svg"),
        QStringLiteral(":/resources/icons/volume-2.svg"),
        QStringLiteral(":/resources/icons/link.svg"),
        QStringLiteral(":/resources/icons/keyboard.svg"),
        QStringLiteral(":/resources/icons/palette.svg"),
        QStringLiteral(":/resources/icons/clock.svg"),
        QStringLiteral(":/resources/icons/terminal.svg"),
        QStringLiteral(":/resources/icons/sliders.svg"),
    };
    for (int i = 0; i < m_navItems.size() && i < iconPaths.size(); ++i) {
        m_navItems.at(i)->setIcon(0, themedNavIcon(iconPaths.at(i), m_dark));
    }
}

void MainWindow::onNavItemClicked(QTreeWidgetItem *item, int /*column*/)
{
    if (!item) {
        return;
    }

    const int page = item->data(0, Qt::UserRole).toInt();
    if (page < 0) {
        item->setExpanded(!item->isExpanded());
        restoreNavSelection();
        return;
    }

    m_activeNavItem = item;
    m_ui->contentStack->setCurrentIndex(page);
}

void MainWindow::restoreNavSelection()
{
    if (!m_activeNavItem) {
        return;
    }
    const QSignalBlocker blocker(m_ui->navTree);
    m_ui->navTree->setCurrentItem(m_activeNavItem);
}

void MainWindow::refreshAllSettingsPages()
{
    m_recognitionModelWidget->refreshFromConfig();
    m_recognitionBehaviorWidget->refreshFromConfig();
    m_ocrSettingsWidget->refreshFromConfig();
    m_llmSettingsWidget->refreshFromConfig();
    m_ttsSettingsWidget->refreshFromConfig();
    m_apiServerSettingsWidget->refreshFromConfig();
    m_shortcutSettingsWidget->refreshFromConfig();
    m_appearanceSettingsWidget->refreshFromConfig();
    if (m_historyWidget) {
        m_historyWidget->refreshHistory();
    }
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(QIcon(":/resources/icons/icon.png"), this);
    m_trayIcon->setToolTip(QStringLiteral("TalkInput"));

    auto *trayMenu = new QMenu(this);
    auto *showAction = trayMenu->addAction(tr("Show Window"));
    connect(showAction, &QAction::triggered, this,
            &MainWindow::onShowMainWindow);
    auto *quitAction = trayMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this,
            &MainWindow::onQuitApplication);

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    onShowMainWindow();
                }
            });
}

void MainWindow::updateControls(bool listening)
{
    const auto &preset =
        appConfig().asrPresets.at(appConfig().settings.asrProviderId);

    if (listening) {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/stop.svg"));
        m_ui->actionStartRecognition->setText(tr("Stop recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Stop recognition"));
        const QString name = QString::fromStdString(preset.name);
        STATUSBAR_INFO("{}", name.isEmpty() ? tr("Listening...")
                                            : tr("Listening — %1").arg(name));
    }
    else {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/mic.svg"));
        m_ui->actionStartRecognition->setText(tr("Start recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Start recognition"));
        if (!m_voiceInputController->isSpeechRecognitionModelLoaded()) {
            STATUSBAR_INFO("{}", tr("No speech recognition model selected"));
        }
        else {
            STATUSBAR_INFO("{}", tr("Speech recognition model: %1")
                                     .arg(QString::fromStdString(preset.name)));
        }
    }
}

void MainWindow::onThemeChanged(ThemeMode mode)
{
    appConfig().settings.theme = themeModeToString(mode);
    markConfigDirty();
    m_dark = applyTheme(mode);
    refreshNavIcons();
    if (m_appearanceSettingsWidget) {
        m_appearanceSettingsWidget->refreshFromConfig();
    }
}

void MainWindow::onLanguageChanged(const QString &language)
{
    appConfig().settings.language = language.toStdString();
    markConfigDirty();
    installAppTranslations(language, this, m_appTranslator, m_qtTranslator);
}

void MainWindow::onToggleSpeechRecognition()
{
    if (!m_voiceInputController) {
        return;
    }

    if (m_voiceInputController->isListening()) {
        m_voiceInputController->stopListening();
        return;
    }

    if (!m_voiceInputController->isSpeechRecognitionModelLoaded()) {
        QMessageBox::warning(this, tr("Speech recognition"),
                             tr("Speech recognition model is still loading.\n\n"
                                "Please wait for it to load, then try again."));
        return;
    }

    m_voiceInputController->startListening();
}

void MainWindow::onRecognizeAudioFile()
{
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Select Audio File"), QString(),
                                     tr("Audio Files (*.wav *.mp3 *.ogg *.flac "
                                        "*.m4a *.aac *.opus);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    STATUSBAR_INFO("{}", tr("Decoding audio..."));
    SPDLOG_INFO("Recognizing file: {}", path);

    const auto decoded = decodeAudioFileToPcm16(path);
    if (!decoded) {
        STATUSBAR_INFO("{}", tr("Failed to decode audio file."));
        return;
    }

    SPDLOG_INFO("Decoded {} bytes of PCM16 from {} at {} Hz channels {}",
                decoded->pcm16.size(), path, decoded->sampleRate,
                decoded->channels);

    if (m_voiceInputController) {
        if (!m_voiceInputController->startSpeechRecognitionSession()) {
            return;
        }
        m_voiceInputController->feedSpeechRecognitionAudio(
            decoded->pcm16, decoded->sampleRate, decoded->channels);
        m_voiceInputController->finishSpeechRecognitionSession();
    }
    STATUSBAR_INFO("{}", tr("Recognition sent to ASR engine"));
}

void MainWindow::onShowMainWindow()
{
    showNormal();
    activateWindow();
    raise();
}

void MainWindow::onQuitApplication()
{
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::onResetSettings()
{
    const QString configPath = QDir::toNativeSeparators(appConfigPath());
    const QMessageBox::StandardButton result = QMessageBox::warning(
        this, tr("Reset Settings"),
        tr("Reset all user settings in this file to bundled defaults?\n\n%1\n\n"
           "Model downloads and recognition history will not be deleted.")
            .arg(configPath),
        QMessageBox::Reset | QMessageBox::Cancel, QMessageBox::Cancel);
    if (result != QMessageBox::Reset) {
        return;
    }

    const QString langBefore = currentAppLanguage();
    if (!resetAppConfigToDefaults()) {
        QMessageBox::warning(this, tr("Reset Settings"),
                             tr("Failed to reset settings."));
        return;
    }

    const QString resetLanguage = currentAppLanguage();
    if (langBefore != resetLanguage) {
        installAppTranslations(resetLanguage, this, m_appTranslator,
                               m_qtTranslator);
    }

    refreshAllSettingsPages();

    m_dark = applyTheme(themeModeFromString(appConfig().settings.theme));
    refreshNavIcons();
    if (m_appearanceSettingsWidget) {
        m_appearanceSettingsWidget->refreshFromConfig();
    }

    STATUSBAR_INFO("{}", tr("Settings reset to defaults"));
}

void MainWindow::onShowAboutDialog()
{
    QMessageBox::about(this, tr("About TalkInput"),
                       tr("<h3>TalkInput %1</h3>"
                          "<p>Local voice input method.</p>"
                          "<table>"
                          "<tr><td>Commit</td><td>%2</td></tr>"
                          "<tr><td>Date</td><td>%3</td></tr>"
                          "</table>")
                           .arg(QApplication::applicationVersion(),
                                QStringLiteral(GIT_COMMIT_ID),
                                QStringLiteral(GIT_COMMIT_DATE)));
}

void MainWindow::onOpenDataDirectory()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(appDataDir()));
}

} // namespace talkinput
