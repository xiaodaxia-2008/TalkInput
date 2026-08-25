#include "main_window.h"
#include "api_server_settings_widget.h"
#include "app_config.h"
#include "appearance_settings_widget.h"
#include "audio_utils.h"
#include "general_settings_widget.h"
#include "history_widget.h"
#include "llm_settings_widget.h"
#include "local_ai_api_server.h"
#include "log_panel.h"
#include "logging.h"
#include "ocr_settings_widget.h"
#include "shortcut_settings_widget.h"
#include "stt_settings_widget.h"
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
#include <QPointer>
#include <QProxyStyle>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleHints>
#include <QSystemTrayIcon>
#include <QTranslator>
#include <QTreeWidget>
#include <QTreeWidgetItem>

namespace zenny
{

namespace
{

class NavTreeStyle final : public QProxyStyle
{
public:
    NavTreeStyle() = default;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget) const override
    {
        if (element == PE_IndicatorBranch) {
            return;
        }
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }
};

} // namespace

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
    updateControls(m_voicePipelineController &&
                   m_voicePipelineController->isListening());
}

void MainWindow::setupUi()
{
    SPDLOG_DEBUG("setupUi: begin");
    m_ui->setupUi(this);
    m_ui->mainSplitter->setStretchFactor(0, 0);
    m_ui->mainSplitter->setStretchFactor(1, 1);
    m_ui->mainSplitter->setSizes({200, 1000});
    installStatusBarLogger(statusBar());
    SPDLOG_DEBUG("setupUi: ui setup complete");

    m_sttSettingsWidget = m_ui->sttSettingsWidget;
    m_ocrSettingsWidget = m_ui->ocrSettingsWidget;
    m_llmSettingsWidget = m_ui->llmSettingsWidget;
    m_ttsSettingsWidget = m_ui->ttsSettingsWidget;
    m_apiServerSettingsWidget = m_ui->apiServerSettingsWidget;
    m_shortcutSettingsWidget = m_ui->shortcutSettingsWidget;
    m_appearanceSettingsWidget = m_ui->appearanceSettingsWidget;
    m_historyWidget = m_ui->historyWidget;
    m_logPanel = m_ui->logPanel;
    m_generalSettingsWidget = m_ui->generalSettingsWidget;
    m_historyWidget->setHistory(&m_history);
    installLogPanelSink(m_logPanel->textEdit());

    m_dark = isDarkTheme(themeModeFromString(appConfig().settings.theme));

    // ── VoicePipelineController (ASR + hotkey + overlay + LLM + text
    // injection) ─
    SPDLOG_DEBUG("setupUi: creating VoicePipelineController");
    m_voicePipelineController = new VoicePipelineController(this);

    // ── Navigation sidebar ─────────────────────────────────────────
    setupNavTree();

    // ── Settings pages ─────────────────────────────────────────────
    m_sttSettingsWidget->setRecognitionActions(m_ui->actionStartRecognition,
                                               m_ui->actionRecognizeFile);

    connect(m_ui->actionStartRecognition, &QAction::triggered, this,
            &MainWindow::onToggleSpeechRecognition);

    connect(m_ui->actionRecognizeFile, &QAction::triggered, this,
            &MainWindow::onRecognizeAudioFile);

    SPDLOG_INFO("Starting ASR service");

    // resultChanged comes from VoicePipelineController → onResult
    connect(m_voicePipelineController,
            &VoicePipelineController::finalTextCommitted, this,
            [this](const QString &text) {
                recordHistoryEntry(text);
                m_sttSettingsWidget->setRecognitionResult(text);
            });
    if (auto *apiServer = LocalAiApiServer::instance()) {
        connect(apiServer, &LocalAiApiServer::transcriptionCompleted, this,
                &MainWindow::recordHistoryEntry);
    }
    connect(m_voicePipelineController,
            &VoicePipelineController::listeningChanged, this,
            [this](bool listening) { updateControls(listening); });
    connect(m_voicePipelineController, &VoicePipelineController::modeChanged,
            this, [this](PipelineMode) {
                if (m_sttSettingsWidget) {
                    m_sttSettingsWidget->updateActiveModeDisplay();
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

void MainWindow::setupNavTree()
{
    SPDLOG_DEBUG("setupNavTree: begin");
    auto *tree = m_ui->navTree;
    tree->setStyle(new NavTreeStyle);
    tree->setRootIsDecorated(false);
    tree->setIndentation(14);

    const auto makeSection = [&](const QString &text) {
        auto *item = new QTreeWidgetItem(tree);
        item->setText(0, text);
        item->setData(0, Qt::UserRole, -1);
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        QFont font = tree->font();
        font.setBold(true);
        item->setFont(0, font);
        m_sectionItems.append(item);
        m_navItems.append(item);
        return item;
    };

    const auto populateItem = [&](QTreeWidgetItem *item, const QString &text,
                                  const QString &iconPath, int page) {
        item->setText(0, text);
        item->setIcon(0, themedNavIcon(iconPath, m_dark));
        item->setData(0, Qt::UserRole, page);
        item->setData(0, Qt::UserRole + 1, iconPath);
        m_navItems.append(item);
    };

    const auto makeChildItem = [&](QTreeWidgetItem *parent, const QString &text,
                                   const QString &iconPath, int page) {
        auto *item = new QTreeWidgetItem(parent);
        populateItem(item, text, iconPath, page);
        return item;
    };

    const auto makeTopItem = [&](const QString &text, const QString &iconPath,
                                 int page) {
        auto *item = new QTreeWidgetItem(tree);
        populateItem(item, text, iconPath, page);
        QFont font = tree->font();
        font.setBold(true);
        item->setFont(0, font);
        return item;
    };

    auto *serviceItem = makeSection(tr("Services"));
    auto *sttItem = makeChildItem(serviceItem, tr("Speech Recognition (STT)"),
                                  QStringLiteral(":/resources/icons/cpu.svg"),
                                  static_cast<int>(SettingsPage::Stt));
    makeChildItem(serviceItem, tr("Text Recognition (OCR)"),
                  QStringLiteral(":/resources/icons/camera.svg"),
                  static_cast<int>(SettingsPage::Ocr));
    makeChildItem(serviceItem, tr("LLM Configuration"),
                  QStringLiteral(":/resources/icons/message-square.svg"),
                  static_cast<int>(SettingsPage::Llm));
    makeChildItem(serviceItem, tr("Speech Synthesis (TTS)"),
                  QStringLiteral(":/resources/icons/volume-2.svg"),
                  static_cast<int>(SettingsPage::Tts));
    makeChildItem(serviceItem, tr("API Server"),
                  QStringLiteral(":/resources/icons/link.svg"),
                  static_cast<int>(SettingsPage::ApiServer));

    makeTopItem(tr("Shortcuts"),
                QStringLiteral(":/resources/icons/keyboard.svg"),
                static_cast<int>(SettingsPage::Shortcut));
    makeTopItem(tr("Appearance"),
                QStringLiteral(":/resources/icons/palette.svg"),
                static_cast<int>(SettingsPage::Appearance));
    makeTopItem(tr("History"), QStringLiteral(":/resources/icons/clock.svg"),
                static_cast<int>(SettingsPage::History));
    makeTopItem(tr("Log"), QStringLiteral(":/resources/icons/terminal.svg"),
                static_cast<int>(SettingsPage::Log));
    makeTopItem(tr("General"), QStringLiteral(":/resources/icons/sliders.svg"),
                static_cast<int>(SettingsPage::General));

    serviceItem->setExpanded(true);

    refreshNavIcons();

    connect(tree, &QTreeWidget::itemClicked, this,
            &MainWindow::onNavItemClicked);

    m_activeNavItem = sttItem;
    tree->setCurrentItem(m_activeNavItem);
    m_ui->contentStack->setCurrentIndex(static_cast<int>(SettingsPage::Stt));
    SPDLOG_DEBUG("setupNavTree: end");
}

void MainWindow::retranslateNav()
{
    const QStringList labels = {tr("Services"),
                                tr("Speech Recognition (STT)"),
                                tr("Text Recognition (OCR)"),
                                tr("LLM Configuration"),
                                tr("Speech Synthesis (TTS)"),
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
    const QColor sectionColor =
        m_dark ? QColor(0x9a, 0x9a, 0x9a) : QColor(0x66, 0x66, 0x66);
    for (auto *section : m_sectionItems) {
        section->setForeground(0, sectionColor);
    }

    for (auto *item : m_navItems) {
        const int page = item->data(0, Qt::UserRole).toInt();
        if (page < 0) {
            continue;
        }
        const QString iconPath = item->data(0, Qt::UserRole + 1).toString();
        if (!iconPath.isEmpty()) {
            item->setIcon(0, themedNavIcon(iconPath, m_dark));
        }
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
    m_sttSettingsWidget->refreshFromConfig();
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

void MainWindow::recordHistoryEntry(const QString &text)
{
    m_history.addEntry(text);
    if (m_historyWidget) {
        m_historyWidget->refreshHistory();
    }
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(QIcon(":/resources/icons/icon.png"), this);
    m_trayIcon->setToolTip(QStringLiteral("Zenny"));

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
        if (!m_voicePipelineController->isSpeechRecognitionModelLoaded()) {
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
    if (!m_voicePipelineController) {
        return;
    }

    if (m_voicePipelineController->isListening()) {
        m_voicePipelineController->stopListening();
        return;
    }

    if (!m_voicePipelineController->isSpeechRecognitionModelLoaded()) {
        QMessageBox::warning(this, tr("Speech recognition"),
                             tr("Speech recognition model is still loading.\n\n"
                                "Please wait for it to load, then try again."));
        return;
    }

    m_voicePipelineController->startListening();
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

    if (!m_voicePipelineController) {
        return;
    }

    const QPointer<MainWindow> window(this);
    STATUSBAR_INFO("{}", tr("Recognition sent to ASR engine"));
    m_voicePipelineController->submitApiTranscription(
        decoded->pcm16, decoded->sampleRate, decoded->channels,
        [window](const ApiTranscriptionResult &result) {
            if (!window) {
                return;
            }
            if (!result.error.isEmpty()) {
                STATUSBAR_ERROR("{}", result.error);
                return;
            }

            window->recordHistoryEntry(result.text);
            window->m_sttSettingsWidget->setRecognitionResult(result.text);
        });
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
    QMessageBox::about(this, tr("About Zenny"),
                       tr("<h3>Zenny %1</h3>"
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

} // namespace zenny
