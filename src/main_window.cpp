#include "main_window.h"
#include "app_config.h"
#include "app_language.h"
#include "asr_config.h"
#include "asr_setting_widget.h"
#include "audio_file_decoder.h"
#include "history_widget.h"
#include "logging.h"
#include "ui_main_window.h"
#include "utils.h"

#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTranslator>

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
    updateControls(m_voiceInput && m_voiceInput->isListening());
}

void MainWindow::setupUi()
{
    SPDLOG_DEBUG("setupUi: begin");
    m_ui->setupUi(this);
    installStatusBarLogger(statusBar());
    SPDLOG_DEBUG("setupUi: ui setup complete");

    // ── VoiceInputController (ASR + hotkey + overlay + LLM + text injection) ─
    SPDLOG_DEBUG("setupUi: creating VoiceInputController");
    m_voiceInput = new VoiceInputController(this);

    connect(
        m_voiceInput, &VoiceInputController::speechRecognitionModelLoadResult,
        this, [this](bool success, const QString &error) {
            if (!success) {
                STATUSBAR_ERROR(
                    "{}",
                    tr("Speech recognition model load failed: %1").arg(error));
            }
            else {
                const auto preset = currentAsrPreset();
                STATUSBAR_INFO("{}", tr("Speech recognition model ready: %1")
                                         .arg(jsonString(preset, "name")));
            }
        });

    // ── History tab ────────────────────────────────────────────
    SPDLOG_DEBUG("setupUi: creating HistoryWidget");
    m_historyWidget = new HistoryWidget(&m_history, m_ui->historyTab);
    m_ui->historyLayout->addWidget(m_historyWidget);
    SPDLOG_DEBUG("setupUi: HistoryWidget added");
    // ── ASR settings tab ────────────────────────────────────────
    setupAsrSettingWidget();

    connect(m_ui->actionStartRecognition, &QAction::triggered, this, [this]() {
        if (m_voiceInput && m_voiceInput->isListening()) {
            m_voiceInput->stopListening();
        }
        else {
            startListening();
        }
    });

    connect(m_ui->actionRecognizeFile, &QAction::triggered, this,
            &MainWindow::onRecognizeFile);

    STATUSBAR_INFO("{}", tr("Loading speech recognition model..."));
    SPDLOG_INFO("Starting ASR service");

    // resultChanged comes from VoiceInputController → onResult
    connect(m_voiceInput, &VoiceInputController::finalTextCommitted, this,
            [this](const QString &text) {
                m_history.addEntry(text);
                if (m_historyWidget) {
                    m_historyWidget->refreshHistory();
                }
            });
    connect(m_voiceInput, &VoiceInputController::listeningChanged, this,
            [this](bool listening) { updateControls(listening); });

    // ── System tray ────────────────────────────────────────────
    SPDLOG_DEBUG("setupUi: setting up tray icon");
    setupTrayIcon();

    if (currentAppLanguage() == QStringLiteral("en")) {
        m_ui->actionEnglish->setChecked(true);
    }
    else {
        m_ui->actionChinese->setChecked(true);
    }

    connect(m_ui->actionChinese, &QAction::triggered, this, [this]() {
        m_ui->actionChinese->setChecked(true);
        m_ui->actionEnglish->setChecked(false);
        onSwitchLanguage(QStringLiteral("zh"));
    });
    connect(m_ui->actionEnglish, &QAction::triggered, this, [this]() {
        m_ui->actionEnglish->setChecked(true);
        m_ui->actionChinese->setChecked(false);
        onSwitchLanguage(QStringLiteral("en"));
    });

    const bool startHidden =
        appConfigBool("/settings/app/startMinimized", false);
    m_ui->actionStartMinimized->setChecked(startHidden);

    connect(m_ui->actionStartMinimized, &QAction::toggled, this,
            [](bool checked) {
                setAppConfigValue("/settings/app/startMinimized", checked);
            });

    connect(m_ui->actionResetSettings, &QAction::triggered, this,
            &MainWindow::resetUserSettings);

    connect(m_ui->actionMoreModels, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/"
                                "releases/tag/asr-models")));
    });
    connect(m_ui->actionAbout, &QAction::triggered, this, [this]() {
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
    });

    connect(m_ui->actionExit, &QAction::triggered, this,
            &MainWindow::quitApplication);

    SPDLOG_DEBUG("setupUi: end");
}

void MainWindow::setupAsrSettingWidget()
{
    if (m_asrSettingWidget) {
        m_ui->asrSettingsLayout->removeWidget(m_asrSettingWidget);
        m_asrSettingWidget->hide();
        m_asrSettingWidget->deleteLater();
        m_asrSettingWidget = nullptr;
    }

    SPDLOG_DEBUG("setupAsrSettingWidget: creating widget");
    m_asrSettingWidget = new AsrSettingWidget(m_ui->asrSettingsTab);
    m_ui->asrSettingsLayout->addWidget(m_asrSettingWidget);
    SPDLOG_DEBUG("setupAsrSettingWidget: widget added");
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(QIcon(":/resources/icons/icon.png"), this);
    m_trayIcon->setToolTip(QStringLiteral("TalkInput"));

    auto *trayMenu = new QMenu(this);
    auto *showAction = trayMenu->addAction(tr("Show Window"));
    connect(showAction, &QAction::triggered, this, [this]() {
        showNormal();
        activateWindow();
        raise();
    });
    auto *quitAction = trayMenu->addAction(tr("Quit"));
    connect(quitAction, &QAction::triggered, this,
            &MainWindow::quitApplication);

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();

    connect(m_trayIcon, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::DoubleClick) {
                    showNormal();
                    activateWindow();
                    raise();
                }
            });
}

void MainWindow::startListening()
{
    if (!m_voiceInput) {
        return;
    }

    if (!m_voiceInput->isSpeechRecognitionModelLoaded()) {
        QMessageBox::warning(this, tr("Speech recognition"),
                             tr("Speech recognition model is still loading.\n\n"
                                "Please wait for it to load, then try again."));
        return;
    }

    m_voiceInput->startListening();
}

void MainWindow::stopListening()
{
    if (m_voiceInput) {
        m_voiceInput->stopListening();
    }
}

void MainWindow::updateControls(bool listening)
{
    const nlohmann::json preset = currentAsrPreset();

    if (listening) {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/stop.svg"));
        m_ui->actionStartRecognition->setText(tr("Stop recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Stop recognition"));
        const QString name = jsonString(preset, "name");
        STATUSBAR_INFO("{}", name.isEmpty() ? tr("Listening...")
                                            : tr("Listening — %1").arg(name));
    }
    else {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/mic.svg"));
        m_ui->actionStartRecognition->setText(tr("Start recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Start recognition"));
        const QString type = jsonString(preset, "type");
        if (!m_voiceInput->isSpeechRecognitionModelLoaded() &&
            type != QStringLiteral("System"))
        {
            STATUSBAR_INFO("{}", tr("No speech recognition model selected"));
        }
        else {
            STATUSBAR_INFO("{}", tr("Speech recognition model: %1")
                                     .arg(jsonString(preset, "name")));
        }
    }
}

void MainWindow::onRecognizeFile()
{
    if (m_voiceInput && !m_voiceInput->acceptsExternalAudio()) {
        STATUSBAR_INFO(
            "{}",
            tr("Selected recognizer does not support audio file recognition."));
        return;
    }

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

    if (m_voiceInput) {
        if (!m_voiceInput->startSpeechRecognitionSession()) {
            return;
        }
        m_voiceInput->feedSpeechRecognitionAudio(
            decoded->pcm16, decoded->sampleRate, decoded->channels);
        m_voiceInput->finishSpeechRecognitionSession();
    }
    STATUSBAR_INFO("{}", tr("Recognition sent to ASR engine"));
}

void MainWindow::quitApplication()
{
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::onSwitchLanguage(const QString &lang)
{
    setAppConfigValue("/settings/app/language", lang);
    installAppTranslations(lang, this, m_appTranslator, m_qtTranslator);
}

void MainWindow::resetUserSettings()
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
        onSwitchLanguage(resetLanguage);
    }

    {
        const QSignalBlocker zhBlocker(m_ui->actionChinese);
        const QSignalBlocker enBlocker(m_ui->actionEnglish);
        const QSignalBlocker startHiddenBlocker(m_ui->actionStartMinimized);
        m_ui->actionChinese->setChecked(resetLanguage != QStringLiteral("en"));
        m_ui->actionEnglish->setChecked(resetLanguage == QStringLiteral("en"));
        m_ui->actionStartMinimized->setChecked(
            appConfigBool("/settings/app/startMinimized", false));
    }

    setupAsrSettingWidget();

    STATUSBAR_INFO("{}", tr("Settings reset to defaults"));
}

} // namespace talkinput
