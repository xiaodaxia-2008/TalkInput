#include "main_window.h"
#include "app_config.h"
#include "app_language.h"
#include "asr_setting_widget.h"
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
#include <QtEndian>

#include <algorithm>

namespace
{

void appendInt16(QByteArray &audioData, qint16 sample)
{
    const qsizetype offset = audioData.size();
    audioData.resize(offset + static_cast<qsizetype>(sizeof(qint16)));
    qToLittleEndian<qint16>(
        sample, reinterpret_cast<uchar *>(audioData.data() + offset));
}

qint16 floatToInt16(float sample)
{
    const float clamped = std::clamp(sample, -1.0F, 1.0F);
    return static_cast<qint16>(clamped * 32767.0F);
}

} // namespace

namespace talkinput
{

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ui(std::make_unique<Ui::MainWindow>())
{
    SPDLOG_DEBUG("MainWindow: constructor begin");
    setupUi();
    SPDLOG_DEBUG("MainWindow: constructor end");
}

MainWindow::~MainWindow()
{
    if (m_asrThread) {
        m_asrThread->quit();
        m_asrThread->wait(5000);
    }
}

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
    SPDLOG_DEBUG("MainWindow::setupUi: begin");
    m_ui->setupUi(this);
    installStatusBarLogger(statusBar());
    SPDLOG_DEBUG("MainWindow::setupUi: ui setup complete");

    // ── ASR Service (persistent worker thread) ────────────────
    SPDLOG_DEBUG("MainWindow::setupUi: creating ASR service thread");
    m_asrService = new AsrService();
    m_asrThread = new QThread(this);
    m_asrService->moveToThread(m_asrThread);
    connect(m_asrThread, &QThread::finished, m_asrService,
            &QObject::deleteLater);
    m_asrThread->start();
    SPDLOG_DEBUG("MainWindow::setupUi: ASR service thread started");

    connect(m_asrService, &AsrService::resultChanged, this,
            &MainWindow::onResult);
    connect(m_asrService, &AsrService::modelLoadResult, this,
            [this](bool success, const QString &error) {
                if (!success) {
                    spdlog::get("statusbar")
                        ->error("{}", tr("Model load failed: %1").arg(error));
                }
                else {
                    spdlog::get("statusbar")->info("{}", tr("Model ready."));
                }
            });

    // ── History tab ────────────────────────────────────────────
    SPDLOG_DEBUG("MainWindow::setupUi: creating HistoryWidget");
    m_historyWidget = new HistoryWidget(&m_history, m_ui->historyTab);
    m_ui->historyLayout->addWidget(m_historyWidget);
    SPDLOG_DEBUG("MainWindow::setupUi: HistoryWidget added");
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

    spdlog::get("statusbar")->info("{}", tr("Loading model..."));
    SPDLOG_INFO("Starting ASR service");

    // ── VoiceInputController (global hotkey, overlay, text injection) ─
    SPDLOG_DEBUG("MainWindow::setupUi: creating VoiceInputController");
    m_voiceInput = new VoiceInputController(m_asrService, &m_history, this);
    qApp->installNativeEventFilter(m_voiceInput);

    connect(m_voiceInput, &VoiceInputController::listeningChanged, this,
            [this](bool listening) { updateControls(listening); });
    connect(m_voiceInput, &VoiceInputController::finalTextCommitted, this,
            [this](const QString &text) {
                if (m_historyWidget) {
                    m_historyWidget->setRealtimeText(text);
                    m_historyWidget->refreshHistory();
                }
            });

    // ── System tray ────────────────────────────────────────────
    SPDLOG_DEBUG("MainWindow::setupUi: setting up tray icon");
    setupTrayIcon();

    m_currentLanguage = currentAppLanguage();
    if (m_currentLanguage == QStringLiteral("en")) {
        m_ui->actionEnglish->setChecked(true);
    }
    else {
        m_ui->actionChinese->setChecked(true);
    }

    connect(m_ui->actionChinese, &QAction::triggered, this, [this]() {
        m_ui->actionChinese->setChecked(true);
        m_ui->actionEnglish->setChecked(false);
        doSwitchLanguage(QStringLiteral("zh"));
    });
    connect(m_ui->actionEnglish, &QAction::triggered, this, [this]() {
        m_ui->actionEnglish->setChecked(true);
        m_ui->actionChinese->setChecked(false);
        doSwitchLanguage(QStringLiteral("en"));
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
    const QString savedAsrName = appConfigString("/settings/asr/name");
    if (!savedAsrName.isEmpty()) {
        const nlohmann::json preset = findAsrPresetByName(savedAsrName);
        const QString type = jsonString(preset, "type");
        const QString dir = type == QStringLiteral("System")
                                ? QString()
                                : QDir(talkinput::appDataDir())
                                      .filePath(QStringLiteral("models/%1")
                                                    .arg(jsonString(
                                                        preset, "modelDirName")));
        const QString name = jsonString(preset, "name");
        if (!name.isEmpty()) {
            SPDLOG_DEBUG("MainWindow::setupUi: restoring saved model {}", name);
            setRecognitionModel(dir, name, type);
            SPDLOG_INFO("Restored model: {} ({})", name, dir);
        }
    }

    SPDLOG_DEBUG("MainWindow::setupUi: end");
}

void MainWindow::setupAsrSettingWidget()
{
    if (m_asrSettingWidget) {
        m_ui->asrSettingsLayout->removeWidget(m_asrSettingWidget);
        m_asrSettingWidget->hide();
        m_asrSettingWidget->deleteLater();
        m_asrSettingWidget = nullptr;
    }

    SPDLOG_DEBUG("MainWindow::setupAsrSettingWidget: creating widget");
    m_asrSettingWidget = new AsrSettingWidget(m_ui->asrSettingsTab);
    m_ui->asrSettingsLayout->addWidget(m_asrSettingWidget);
    SPDLOG_DEBUG("MainWindow::setupAsrSettingWidget: widget added");

    connect(
        m_asrSettingWidget, &AsrSettingWidget::modelSelected, this,
        [this](const QString &dir, const QString &name, const QString &type) {
            setRecognitionModel(dir, name, type);
            m_ui->tabWidget->setCurrentWidget(m_ui->historyTab);
        });
    connect(m_asrSettingWidget, &AsrSettingWidget::hotwordsChanged, this,
            [this]() {
                if (m_currentModelDirectory.isEmpty()) {
                    return;
                }
                SPDLOG_INFO("Hot words changed, reloading ASR model...");
                spdlog::get("statusbar")
                    ->info("{}", tr("Hot words saved, reloading model..."));
                QMetaObject::invokeMethod(m_asrService, "loadModel",
                                          Qt::QueuedConnection);
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
    if (!m_asrService->isModelLoaded()) {
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
        spdlog::get("statusbar")
            ->info("{}", m_currentModelName.isEmpty()
                             ? tr("Listening...")
                             : tr("Listening — %1").arg(m_currentModelName));
        if (m_historyWidget) {
            m_historyWidget->setListening(true);
        }
    }
    else {
        m_ui->actionStartRecognition->setIcon(
            QIcon(":/resources/icons/mic.svg"));
        m_ui->actionStartRecognition->setText(tr("Start recognition"));
        m_ui->actionStartRecognition->setToolTip(tr("Start recognition"));
        if (m_currentModelDirectory.isEmpty() &&
            m_currentModelType != QStringLiteral("System"))
        {
            spdlog::get("statusbar")->info("{}", tr("No model selected"));
        }
        else {
            spdlog::get("statusbar")
                ->info("{}", tr("Model: %1").arg(m_currentModelName));
        }
        if (m_historyWidget) {
            m_historyWidget->setListening(false);
        }
    }
}

void MainWindow::setRecognitionModel(const QString &modelDirectory,
                                     const QString &modelName,
                                     const QString &modelType)
{
    const QString normalizedDirectory =
        QDir::fromNativeSeparators(modelDirectory.trimmed());
    m_currentModelDirectory = normalizedDirectory.isEmpty()
                                  ? QString()
                                  : QDir::cleanPath(normalizedDirectory);
    m_currentModelName = modelName.trimmed();
    m_currentModelType = modelType.trimmed();

    if (m_currentModelName.isEmpty()) {
        m_currentModelName =
            m_currentModelType == QStringLiteral("System")
                ? tr("System Speech")
                : QFileInfo(m_currentModelDirectory).fileName();
    }

    if (m_currentModelDirectory.isEmpty() &&
        m_currentModelType != QStringLiteral("System"))
    {
        spdlog::get("statusbar")->info("{}", tr("No model selected"));
    }
    else {
        spdlog::get("statusbar")->info("{}", tr("Loading model..."));
    }
    SPDLOG_INFO("Recognition model set: {} ({})", m_currentModelName,
                m_currentModelDirectory);

    setAppConfigValue("/settings/asr/name", m_currentModelName);

    if (m_asrService) {
        m_asrService->setModelDirectory(m_currentModelDirectory);
        m_asrService->setModelType(m_currentModelType);
        QMetaObject::invokeMethod(m_asrService, "loadModel",
                                  Qt::QueuedConnection);
    }
}

void MainWindow::onResult(const QString &text, bool isFinal)
{
    if (isFinal) {
        SPDLOG_INFO("[final] {}", text);
    }

    if (!isFinal && !text.trimmed().isEmpty()) {
        m_historyWidget->setRealtimeText(text);
    }

    if (isFinal && !text.trimmed().isEmpty()) {
        m_historyWidget->setRealtimeText(text);
        QTimer::singleShot(0, m_historyWidget, &HistoryWidget::refreshHistory);
    }

    if (isFinal) {
        spdlog::get("statusbar")
            ->info("{}", m_currentModelName.isEmpty()
                             ? tr("Listening...")
                             : tr("Listening — %1").arg(m_currentModelName));
    }
}

void MainWindow::onRecognizeFile()
{
    if (m_asrService && !m_asrService->acceptsExternalAudio()) {
        spdlog::get("statusbar")
            ->info("{}", tr("Selected recognizer does not support audio file "
                            "recognition."));
        return;
    }

    const QString path =
        QFileDialog::getOpenFileName(this, tr("Select Audio File"), QString(),
                                     tr("Audio Files (*.wav *.mp3 *.ogg *.flac "
                                        "*.m4a *.aac *.opus);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    spdlog::get("statusbar")->info("{}", tr("Decoding audio..."));
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

        if (format.sampleFormat() == QAudioFormat::Int16) {
            allPcm.append(
                reinterpret_cast<const char *>(buf.constData<int16_t>()),
                buf.byteCount());
        }
        else if (format.sampleFormat() == QAudioFormat::Float) {
            for (int i = 0; i < buf.sampleCount(); ++i) {
                appendInt16(allPcm, floatToInt16(buf.constData<float>()[i]));
            }
        }
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
        spdlog::get("statusbar")
            ->info("{}", tr("Failed to decode audio file."));
        return;
    }

    if (decodedSampleRate <= 0 || decodedChannels <= 0) {
        spdlog::get("statusbar")
            ->info("{}", tr("Failed to decode audio file."));
        return;
    }

    SPDLOG_INFO("Decoded {} bytes of PCM16 from {} at {} Hz channels {}",
                allPcm.size(), path, decodedSampleRate, decodedChannels);

    QMetaObject::invokeMethod(
        m_asrService,
        [this, allPcm, decodedSampleRate, decodedChannels]() {
            m_asrService->startSession();
            m_asrService->feedAudio(allPcm, decodedSampleRate, decodedChannels);
            m_asrService->finishSession();
        },
        Qt::QueuedConnection);
    spdlog::get("statusbar")->info("{}", tr("Recognition sent to ASR engine."));
}

void MainWindow::quitApplication()
{
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::doSwitchLanguage(const QString &lang)
{
    m_currentLanguage = lang;
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

    if (!resetAppConfigToDefaults()) {
        QMessageBox::warning(this, tr("Reset Settings"),
                             tr("Failed to reset settings."));
        return;
    }

    const QString resetLanguage = currentAppLanguage();
    if (m_currentLanguage != resetLanguage) {
        doSwitchLanguage(resetLanguage);
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

    const QString savedAsrName = appConfigString("/settings/asr/name");
    if (!savedAsrName.isEmpty()) {
        const nlohmann::json preset = findAsrPresetByName(savedAsrName);
        const QString type = jsonString(preset, "type");
        const QString dir = type == QStringLiteral("System")
                                ? QString()
                                : QDir(talkinput::appDataDir())
                                      .filePath(QStringLiteral("models/%1")
                                                    .arg(jsonString(
                                                        preset, "modelDirName")));
        const QString name = jsonString(preset, "name");
        if (!name.isEmpty()) {
            setRecognitionModel(dir, name, type);
        }
    }
    else {
        m_currentModelDirectory.clear();
        m_currentModelName.clear();
        m_currentModelType.clear();
        if (m_asrService) {
            m_asrService->setModelDirectory({});
            m_asrService->setModelType({});
            QMetaObject::invokeMethod(m_asrService, "unloadModel",
                                      Qt::QueuedConnection);
        }
        spdlog::get("statusbar")->info("{}", tr("No model selected"));
    }

    spdlog::get("statusbar")->info("{}", tr("Settings reset to defaults."));
}

} // namespace talkinput
