#include "main_window.h"
#include "app_config.h"
#include "app_language.h"
#include "asr_config.h"
#include "asr_setting_widget.h"
#include "audio_utils.h"
#include "history_widget.h"
#include "logging.h"
#include "ui_main_window.h"
#include "utils.h"

#include <QAction>
#include <QApplication>
#include <QAudioDecoder>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QTranslator>

namespace talkinput
{
namespace
{

void showStatusMessage(const QString &message)
{
    spdlog::get("statusbar")->info("{}", message);
}

void showStatusError(const QString &message)
{
    spdlog::get("statusbar")->error("{}", message);
}

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

    connect(m_voiceInput, &VoiceInputController::modelLoadResult, this,
            [this](bool success, const QString &error) {
                if (!success) {
                    showStatusError(tr("Model load failed: %1").arg(error));
                }
                else {
                    const auto preset = currentAsrPreset();
                    showStatusMessage(
                        tr("Model ready: %1").arg(jsonString(preset, "name")));
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

    showStatusMessage(tr("Loading model..."));
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

    // ── Restore persisted state & load model ────────────────────
    loadConfiguredAsrModel(false);

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

    connect(m_asrSettingWidget, &AsrSettingWidget::hotwordsChanged, this,
            [this]() {
                SPDLOG_INFO("Hot words changed, reloading ASR model...");
                showStatusMessage(tr("Hot words saved, reloading model..."));
                if (m_voiceInput) {
                    const nlohmann::json preset = currentAsrPreset();
                    if (preset.is_object()) {
                        m_voiceInput->loadModel(preset);
                    }
                }
            });
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
    if (!m_voiceInput->isModelLoaded()) {
        QMessageBox::warning(
            this, tr("Speech recognition"),
            tr("Model is still loading.\n\n"
               "Please wait for the model to load, then try again."));
        return;
    }

    if (m_voiceInput) {
        m_voiceInput->startListening();
    }
}

void MainWindow::stopListening()
{
    if (m_voiceInput) {
        m_voiceInput->stopListening();
    }
}

void MainWindow::updateControls(bool listening)
{
    if (listening) {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/stop.svg"));
        m_ui->actionStartRecognition->setText(tr("Stop recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Stop recognition"));
        const QString name = jsonString(currentAsrPreset(), "name");
        showStatusMessage(name.isEmpty() ? tr("Listening...")
                                         : tr("Listening — %1").arg(name));
    }
    else {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/mic.svg"));
        m_ui->actionStartRecognition->setText(tr("Start recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Start recognition"));
        const QString type = jsonString(currentAsrPreset(), "type");
        if (!m_voiceInput->isModelLoaded() && type != QStringLiteral("System"))
        {
            showStatusMessage(tr("No model selected"));
        }
        else {
            showStatusMessage(
                tr("Model: %1").arg(jsonString(currentAsrPreset(), "name")));
        }
    }
}

void MainWindow::onRecognizeFile()
{
    if (m_voiceInput && !m_voiceInput->acceptsExternalAudio()) {
        showStatusMessage(
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

    showStatusMessage(tr("Decoding audio..."));
    SPDLOG_INFO("Recognizing file: {}", path);

    auto *decoder = new QAudioDecoder(this);
    QEventLoop loop;

    QByteArray allPcm;
    bool ok = false;
    int decodedSampleRate = 0;
    int decodedChannels = 0;

    connect(decoder, &QAudioDecoder::bufferReady, this, [&]() {
        const QAudioBuffer buf = decoder->read();
        const QAudioFormat format = buf.format();
        if (decodedSampleRate == 0) {
            decodedSampleRate = format.sampleRate();
            decodedChannels = format.channelCount();
        }
        else if (decodedSampleRate != format.sampleRate() ||
                 decodedChannels != format.channelCount())
        {
            SPDLOG_WARN("Audio decoder format changed from {} channels {} "
                        "to {} channels {}",
                        decodedSampleRate, decodedChannels, format.sampleRate(),
                        format.channelCount());
        }

        const QByteArray audioData(buf.constData<char>(), buf.byteCount());
        allPcm.append(convertAudioToPcm16(audioData, format));
    });

    connect(decoder, &QAudioDecoder::finished, this, [&]() {
        ok = true;
        loop.quit();
    });

    connect(decoder,
            static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(
                &QAudioDecoder::error),
            this, [&](QAudioDecoder::Error) {
                SPDLOG_ERROR("Audio decoder error: {}", decoder->errorString());
                loop.quit();
            });

    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);

    decoder->setSource(QUrl::fromLocalFile(path));
    decoder->start();

    timeoutTimer.start(30000);
    loop.exec();

    decoder->stop();
    decoder->deleteLater();

    if (!ok || allPcm.isEmpty()) {
        showStatusMessage(tr("Failed to decode audio file."));
        return;
    }

    if (decodedSampleRate <= 0 || decodedChannels <= 0) {
        showStatusMessage(tr("Failed to decode audio file."));
        return;
    }

    SPDLOG_INFO("Decoded {} bytes of PCM16 from {} at {} Hz channels {}",
                allPcm.size(), path, decodedSampleRate, decodedChannels);

    if (m_voiceInput) {
        m_voiceInput->startSession();
        m_voiceInput->feedAudio(allPcm, decodedSampleRate, decodedChannels);
        m_voiceInput->finishSession();
    }
    showStatusMessage(tr("Recognition sent to ASR engine"));
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

    loadConfiguredAsrModel(true);

    showStatusMessage(tr("Settings reset to defaults"));
}

void MainWindow::loadConfiguredAsrModel(bool reportNoModel)
{
    if (!m_voiceInput) {
        return;
    }

    const nlohmann::json preset = currentAsrPreset();
    const QString name = jsonString(preset, "name");
    if (preset.is_object() && !name.isEmpty()) {
        SPDLOG_DEBUG("MainWindow: loading configured ASR model {}", name);
        m_voiceInput->loadModel(preset);
        SPDLOG_INFO("Configured ASR model loaded: {} ({})", name,
                    asrModelDir(preset));
        return;
    }

    m_voiceInput->unloadModel();
    if (reportNoModel) {
        showStatusMessage(tr("No model selected"));
    }
}

} // namespace talkinput
