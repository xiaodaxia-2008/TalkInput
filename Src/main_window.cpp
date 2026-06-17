#include "main_window.h"
#include "asr_setting_widget.h"
#include "history_widget.h"
#include "logging.h"
#include "ui_main_window.h"

#include <QAction>
#include <QApplication>
#include <QAudioDecoder>
#include <QDesktopServices>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeySequence>
#include <QLibraryInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QToolBar>
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
    setupUi();
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

void MainWindow::setupUi()
{
    m_ui->setupUi(this);

    // ── ASR Service (persistent worker thread) ────────────────
    m_asrService = new AsrService();
    m_asrThread = new QThread(this);
    m_asrService->moveToThread(m_asrThread);
    connect(m_asrThread, &QThread::finished, m_asrService,
            &QObject::deleteLater);
    m_asrThread->start();

    connect(m_asrService, &AsrService::resultChanged, this,
            &MainWindow::onResult);
    connect(m_asrService, &AsrService::modelLoadResult, this,
            [this](bool success, const QString &error) {
                if (!success) {
                    spdlog::error("ASR model load failed: {}", error);
                    statusBar()->showMessage(
                        tr("Model load failed: %1").arg(error));
                }
                else {
                    spdlog::info("ASR model loaded successfully");
                    statusBar()->showMessage(tr("Model ready."));
                }
            });

    // ── History tab ────────────────────────────────────────────
    m_historyWidget = new HistoryWidget(&m_history, m_ui->historyTab);
    m_ui->historyLayout->addWidget(m_historyWidget);
    connect(
        m_historyWidget, &HistoryWidget::statusMessage, this,
        [this](const QString &msg) { statusBar()->showMessage(msg, 2000); });

    // ── ASR settings tab ────────────────────────────────────────
    m_asrSettingWidget = new AsrSettingWidget(m_ui->asrSettingsTab);
    m_ui->asrSettingsLayout->addWidget(m_asrSettingWidget);
    connect(m_asrSettingWidget, &AsrSettingWidget::modelSelected, this,
            [this](const QString &dir, const QString &name) {
                setRecognitionModel(dir, name);
                m_ui->tabWidget->setCurrentWidget(m_ui->historyTab);
            });
    connect(m_asrSettingWidget, &AsrSettingWidget::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg); });
    connect(m_asrSettingWidget, &AsrSettingWidget::punctuationModelReady, this,
            [this]() {
                if (m_currentModelDirectory.isEmpty()) {
                    return;
                }
                spdlog::info("Punctuation model ready, reloading ASR model...");
                statusBar()->showMessage(
                    tr("Punctuation ready, reloading model..."));
                QMetaObject::invokeMethod(m_asrService, "loadModel",
                                          Qt::QueuedConnection);
            });
    connect(
        m_asrSettingWidget, &AsrSettingWidget::hotwordsChanged, this, [this]() {
            if (m_currentModelDirectory.isEmpty()) {
                return;
            }
            spdlog::info("Hot words changed, reloading ASR model...");
            statusBar()->showMessage(tr("Hot words saved, reloading model..."));
            QMetaObject::invokeMethod(m_asrService, "loadModel",
                                      Qt::QueuedConnection);
        });

    // ── Toolbar ────────────────────────────────────────────────
    m_recognitionToolBar = addToolBar(tr("Recognition"));
    m_recognitionToolBar->setObjectName("recognitionToolBar");
    m_recognitionToolBar->setMovable(false);
    m_recognitionToolBar->setIconSize(QSize(28, 28));
    m_recognitionToolBar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    m_startAction = m_recognitionToolBar->addAction(
        QIcon(":/resources/mic.svg"), tr("Start recognition"));
    m_startAction->setToolTip(tr("Start recognition"));
    connect(m_startAction, &QAction::triggered, this, [this]() {
        if (m_voiceInput && m_voiceInput->isListening()) {
            m_voiceInput->stopListening();
        }
        else {
            startListening();
        }
    });

    m_fileAction = m_recognitionToolBar->addAction(
        QIcon(":/resources/folder-plus.svg"), tr("Recognize file"));
    m_fileAction->setToolTip(tr("Import an audio file for recognition"));
    connect(m_fileAction, &QAction::triggered, this,
            &MainWindow::onRecognizeFile);

    statusBar()->showMessage(tr("Loading model..."));
    spdlog::info("Starting ASR service");

    // ── VoiceInputController (global hotkey, overlay, text injection) ─
    m_voiceInput = new VoiceInputController(m_asrService, &m_history, this);
    qApp->installNativeEventFilter(m_voiceInput);

    connect(m_voiceInput, &VoiceInputController::listeningChanged, this,
            [this](bool listening) { updateControls(listening); });
    connect(m_voiceInput, &VoiceInputController::statusMessage, this,
            [this](const QString &msg) { statusBar()->showMessage(msg); });

    // ── System tray ────────────────────────────────────────────
    setupTrayIcon();

    // ── Menu bar ────────────────────────────────────────────────
    m_prefMenu = menuBar()->addMenu(tr("Preferences"));
    m_langMenu = m_prefMenu->addMenu(
        QIcon(QStringLiteral(":/resources/globe.svg")), tr("Language"));

    m_zhAction = m_langMenu->addAction(
        QIcon(QStringLiteral(":/resources/zh.svg")), tr("Chinese"));
    m_zhAction->setCheckable(true);
    m_enAction = m_langMenu->addAction(
        QIcon(QStringLiteral(":/resources/en.svg")), tr("English"));
    m_enAction->setCheckable(true);

    QSettings langS;
    m_currentLanguage =
        langS.value(QStringLiteral("app/language"), QStringLiteral("zh"))
            .toString();
    if (m_currentLanguage == QStringLiteral("en")) {
        m_enAction->setChecked(true);
    }
    else {
        m_zhAction->setChecked(true);
    }

    connect(m_zhAction, &QAction::triggered, this, [this]() {
        m_zhAction->setChecked(true);
        m_enAction->setChecked(false);
        doSwitchLanguage(QStringLiteral("zh"));
    });
    connect(m_enAction, &QAction::triggered, this, [this]() {
        m_enAction->setChecked(true);
        m_zhAction->setChecked(false);
        doSwitchLanguage(QStringLiteral("en"));
    });

    m_prefMenu->addSeparator();
    m_startHiddenAction = m_prefMenu->addAction(tr("Start minimized"));
    m_startHiddenAction->setCheckable(true);

    QSettings startupS;
    const bool startHidden =
        startupS.value(QStringLiteral("app/startMinimized"), false).toBool();
    m_startHiddenAction->setChecked(startHidden);

    connect(m_startHiddenAction, &QAction::toggled, this, [](bool checked) {
        QSettings s;
        s.setValue(QStringLiteral("app/startMinimized"), checked);
    });

    m_helpMenu = menuBar()->addMenu(tr("Help"));
    auto *modelsAction = m_helpMenu->addAction(QStringLiteral("More Models"));
    connect(modelsAction, &QAction::triggered, this, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/k2-fsa/sherpa-onnx/"
                                "releases/tag/asr-models")));
    });
    auto *aboutAction = m_helpMenu->addAction(tr("About"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
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

    m_exitAction = menuBar()->addAction(tr("Exit"));
    m_exitAction->setShortcut(QKeySequence::Quit);
    connect(m_exitAction, &QAction::triggered, this,
            &MainWindow::quitApplication);

    // ── Restore persisted state & load model ────────────────────
    QSettings s;
    const QString savedDir =
        s.value(QStringLiteral("model/directory")).toString();
    const QString savedName = s.value(QStringLiteral("model/name")).toString();
    if (!savedDir.isEmpty()) {
        setRecognitionModel(savedDir, savedName);
        spdlog::info("Restored model: {} ({})", savedName, savedDir);
    }
}

void MainWindow::setupTrayIcon()
{
    m_trayIcon = new QSystemTrayIcon(
        QIcon(QStringLiteral(":/resources/icon.png")), this);
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
        if (m_startAction) {
            m_startAction->setIcon(QIcon(":/resources/stop.svg"));
            m_startAction->setText(tr("Stop recognition"));
            m_startAction->setToolTip(tr("Stop recognition"));
        }
        statusBar()->showMessage(
            m_currentModelName.isEmpty()
                ? tr("Listening...")
                : tr("Listening — %1").arg(m_currentModelName));
        if (m_historyWidget) {
            m_historyWidget->setListening(true);
        }
    }
    else {
        if (m_startAction) {
            m_startAction->setIcon(QIcon(":/resources/mic.svg"));
            m_startAction->setText(tr("Start recognition"));
            m_startAction->setToolTip(tr("Start recognition"));
        }
        if (m_currentModelDirectory.isEmpty()) {
            statusBar()->showMessage(tr("No model selected"));
        }
        else {
            statusBar()->showMessage(tr("Model: %1").arg(m_currentModelName));
        }
        if (m_historyWidget) {
            m_historyWidget->setListening(false);
        }
    }
}

void MainWindow::setRecognitionModel(const QString &modelDirectory,
                                     const QString &modelName)
{
    const QString normalizedDirectory =
        QDir::fromNativeSeparators(modelDirectory.trimmed());
    m_currentModelDirectory = normalizedDirectory.isEmpty()
                                  ? QString()
                                  : QDir::cleanPath(normalizedDirectory);
    m_currentModelName = modelName.trimmed();

    if (m_currentModelName.isEmpty()) {
        m_currentModelName = QFileInfo(m_currentModelDirectory).fileName();
    }

    if (m_currentModelDirectory.isEmpty()) {
        statusBar()->showMessage(tr("No model selected"));
    }
    else {
        statusBar()->showMessage(tr("Loading model..."));
    }
    spdlog::info("Recognition model set: {} ({})", m_currentModelName,
                 m_currentModelDirectory);

    QSettings s;
    s.setValue(QStringLiteral("model/directory"), m_currentModelDirectory);
    s.setValue(QStringLiteral("model/name"), m_currentModelName);

    if (m_asrService) {
        m_asrService->setModelDirectory(m_currentModelDirectory);
        QMetaObject::invokeMethod(m_asrService, "loadModel",
                                  Qt::QueuedConnection);
    }
}

void MainWindow::onResult(const QString &text, bool isFinal)
{
    spdlog::info("{} {}", isFinal ? "[final]" : "[partial]", text);

    if (!isFinal && !text.trimmed().isEmpty()) {
        m_historyWidget->setRealtimeText(text);
    }

    if (isFinal && !text.trimmed().isEmpty()) {
        m_historyWidget->setRealtimeText(text);
        QTimer::singleShot(0, m_historyWidget, &HistoryWidget::refreshHistory);
    }

    if (auto *sb = statusBar()) {
        if (isFinal) {
            sb->showMessage(m_currentModelName.isEmpty()
                                ? tr("Listening...")
                                : tr("Listening — %1").arg(m_currentModelName));
        }
    }
}

void MainWindow::onRecognizeFile()
{
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Select Audio File"), QString(),
                                     tr("Audio Files (*.wav *.mp3 *.ogg *.flac "
                                        "*.m4a *.aac *.opus);;All Files (*)"));
    if (path.isEmpty()) {
        return;
    }

    statusBar()->showMessage(tr("Decoding audio..."));
    spdlog::info("Recognizing file: {}", path);

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
            spdlog::warn("Audio decoder format changed from {} channels {} "
                         "to {} channels {}",
                         decodedSampleRate, decodedChannels,
                         format.sampleRate(), format.channelCount());
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
                spdlog::error("Audio decoder error: {}",
                              decoder->errorString());
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
        statusBar()->showMessage(tr("Failed to decode audio file."), 5000);
        return;
    }

    if (decodedSampleRate <= 0 || decodedChannels <= 0) {
        statusBar()->showMessage(tr("Failed to decode audio file."), 5000);
        return;
    }

    spdlog::info("Decoded {} bytes of PCM16 from {} at {} Hz channels {}",
                 allPcm.size(), path, decodedSampleRate, decodedChannels);

    QMetaObject::invokeMethod(
        m_asrService,
        [this, allPcm, decodedSampleRate, decodedChannels]() {
            m_asrService->startSession();
            m_asrService->feedAudio(allPcm, decodedSampleRate, decodedChannels);
            m_asrService->finishSession();
        },
        Qt::QueuedConnection);
    statusBar()->showMessage(tr("Recognition sent to ASR engine."), 3000);
}

void MainWindow::quitApplication()
{
    m_forceQuit = true;
    qApp->quit();
}

void MainWindow::retranslateUi()
{
    m_ui->retranslateUi(this);
    if (m_historyWidget) {
        m_historyWidget->retranslateUi();
    }

    m_recognitionToolBar->setWindowTitle(tr("Recognition"));
    if (m_fileAction) {
        m_fileAction->setText(tr("Recognize file"));
        m_fileAction->setToolTip(tr("Import an audio file for recognition"));
    }
    if (m_startAction) {
        const bool listening = m_voiceInput && m_voiceInput->isListening();
        m_startAction->setText(listening ? tr("Stop recognition")
                                         : tr("Start recognition"));
        m_startAction->setToolTip(listening ? tr("Stop recognition")
                                            : tr("Start recognition"));
    }

    m_exitAction->setText(tr("Exit"));
    m_prefMenu->setTitle(tr("Preferences"));
    m_langMenu->setTitle(tr("Language"));
    m_zhAction->setText(tr("Chinese"));
    m_enAction->setText(tr("English"));
    m_startHiddenAction->setText(tr("Start minimized"));
    m_helpMenu->setTitle(tr("Help"));

    statusBar()->showMessage(m_currentModelDirectory.isEmpty()
                                 ? tr("No model selected")
                                 : tr("Model: %1").arg(m_currentModelName));
}

void MainWindow::doSwitchLanguage(const QString &lang)
{
    m_currentLanguage = lang;
    QSettings s;
    s.setValue(QStringLiteral("app/language"), lang);

    // Remove old translators
    if (m_appTranslator) {
        QApplication::removeTranslator(m_appTranslator);
        m_appTranslator->deleteLater();
        m_appTranslator = nullptr;
    }
    if (m_qtTranslator) {
        QApplication::removeTranslator(m_qtTranslator);
        m_qtTranslator->deleteLater();
        m_qtTranslator = nullptr;
    }

    if (lang == QStringLiteral("zh")) {
        auto *appT = new QTranslator(this);
        if (appT->load(QStringLiteral(":/i18n/TalkInput_zh.qm"))) {
            m_appTranslator = appT;
            QApplication::installTranslator(m_appTranslator);
        }
        else {
            delete appT;
        }

        auto *qtT = new QTranslator(this);
        if (qtT->load(QStringLiteral("qt_zh_CN"),
                      QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        {
            m_qtTranslator = qtT;
            QApplication::installTranslator(m_qtTranslator);
        }
        else {
            delete qtT;
        }
    }

    retranslateUi();
}

} // namespace talkinput
