#include "main_window.h"
#include "setting_widget.h"
#include "ui_main_window.h"

#include <QSettings>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioSource>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QAction>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <cstring>

#include <spdlog/common.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include "qt_fmt.h"

// ── Icon helpers ──────────────────────────────────────────────────

namespace {

void applyIcon(QPushButton *btn, const QString &svgPath, int size) {
  btn->setIcon(QIcon(svgPath));
  btn->setIconSize(QSize(size, size));
  btn->setText({});
}

// ── Utility functions ───────────────────────────────────────────

void appendInt16(QByteArray &audioData, qint16 sample) {
  const qsizetype offset = audioData.size();
  audioData.resize(offset + static_cast<qsizetype>(sizeof(qint16)));
  qToLittleEndian<qint16>(sample,
                          reinterpret_cast<uchar *>(audioData.data() + offset));
}

qint16 floatToInt16(float sample) {
  const float clamped = std::clamp(sample, -1.0F, 1.0F);
  return static_cast<qint16>(clamped * 32767.0F);
}

QByteArray convertToPcm16(const QByteArray &audioData,
                          const QAudioFormat &format) {
  if (audioData.isEmpty())
    return {};

  if (format.sampleFormat() == QAudioFormat::Int16)
    return audioData;

  QByteArray pcm16;

  switch (format.sampleFormat()) {
  case QAudioFormat::UInt8:
    pcm16.reserve(audioData.size() * 2);
    for (const char byte : audioData) {
      const auto sample = static_cast<unsigned char>(byte);
      appendInt16(pcm16,
                  static_cast<qint16>((static_cast<int>(sample) - 128) << 8));
    }
    break;
  case QAudioFormat::Int32: {
    const int sampleCount = audioData.size() / static_cast<int>(sizeof(qint32));
    pcm16.reserve(sampleCount * 2);
    const auto *data = reinterpret_cast<const uchar *>(audioData.constData());
    for (int i = 0; i < sampleCount; ++i) {
      const qint32 sample =
          qFromLittleEndian<qint32>(data + i * sizeof(qint32));
      appendInt16(pcm16, static_cast<qint16>(sample >> 16));
    }
    break;
  }
  case QAudioFormat::Float: {
    const int sampleCount = audioData.size() / static_cast<int>(sizeof(float));
    pcm16.reserve(sampleCount * 2);
    for (int i = 0; i < sampleCount; ++i) {
      float sample = 0.0F;
      std::memcpy(&sample,
                  audioData.constData() + i * static_cast<int>(sizeof(float)),
                  sizeof(float));
      appendInt16(pcm16, floatToInt16(sample));
    }
    break;
  }
  default:
    break;
  }

  return pcm16;
}

QString findModelFile(const QDir &dir, const QStringList &patterns) {
  const auto files = dir.entryInfoList(patterns, QDir::Files, QDir::Name);
  if (files.isEmpty())
    return {};
  for (const QFileInfo &fi : files)
    if (fi.fileName().contains(QStringLiteral("int8")))
      return fi.absoluteFilePath();
  return files.first().absoluteFilePath();
}

bool detectModelFiles(const QString &modelDir, QString &encoder,
                      QString &decoder, QString &joiner, QString &tokens) {
  const QDir dir(modelDir);
  encoder = findModelFile(dir, {QStringLiteral("*encoder*")});
  decoder = findModelFile(dir, {QStringLiteral("*decoder*")});
  joiner = findModelFile(dir, {QStringLiteral("*joiner*")});
  tokens = findModelFile(dir, {QStringLiteral("tokens.txt")});
  return !encoder.isEmpty() && !decoder.isEmpty() && !joiner.isEmpty() &&
         !tokens.isEmpty();
}

} // namespace

namespace talkinput {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ui(std::make_unique<Ui::MainWindow>()) {
  setupUi();

  connect(&m_recognizer, &SpeechRecognizer::resultChanged, this,
          &MainWindow::onResult);
}

MainWindow::~MainWindow() {
  stopListening();
}

void MainWindow::setupUi() {
  m_ui->setupUi(this);

  // ── SettingWidget (Models tab) ────────────────────────────
  m_settingWidget = new SettingWidget(m_ui->modelsTab);
  m_ui->modelsLayout->addWidget(m_settingWidget);
  connect(m_settingWidget, &SettingWidget::modelSelected,
          this, [this](const QString &dir, const QString &name) {
            setRecognitionModel(dir, name);
            m_ui->tabWidget->setCurrentWidget(m_ui->recognitionTab);
          });
  connect(m_settingWidget, &SettingWidget::statusMessage,
          this, [this](const QString &msg) {
            statusBar()->showMessage(msg);
          });

  // ── spdlog ─────────────────────────────────────────────────
  auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  consoleSink->set_level(spdlog::level::trace);

  const QString logDir = QStandardPaths::writableLocation(
      QStandardPaths::AppDataLocation);
  QDir().mkpath(logDir);
  auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
      QDir(logDir).filePath("talkinput.log").toStdString(), true);
  fileSink->set_level(spdlog::level::trace);

  auto logger = std::make_shared<spdlog::logger>(
      "talkinput", spdlog::sinks_init_list{consoleSink, fileSink});
  logger->set_level(spdlog::level::trace);
  logger->set_pattern("[%H:%M:%S %L] %v");
  spdlog::set_default_logger(logger);
  spdlog::set_level(spdlog::level::trace);

  // ── Buttons ────────────────────────────────────────────────
  applyIcon(m_ui->startButton, ":/resources/mic.svg", 28);

  // ── Connections ────────────────────────────────────────────
  connect(m_ui->startButton, &QPushButton::clicked, this,
          &MainWindow::toggleListening);

  connect(&m_modelLoadWatcher, &QFutureWatcher<bool>::finished, this,
          &MainWindow::onModelLoaded);

  // ── Style ──────────────────────────────────────────────────
  m_ui->startButton->setStyleSheet(
      "QPushButton { background: transparent; border: 2px solid #999; "
      "border-radius: 24px; }"
      "QPushButton:hover { border-color: #555; background: #f5f5f5; }"
      "QPushButton:pressed { background: #e0e0e0; }");

  statusBar()->showMessage(tr("Ready."));
  spdlog::info("sherpa-onnx is linked from local release libraries.");

  // ── Menu bar ────────────────────────────────────────────────
  auto *helpMenu = menuBar()->addMenu(tr("Help"));
  auto *aboutAction = helpMenu->addAction(tr("About"));
  connect(aboutAction, &QAction::triggered, this, [this]() {
    QMessageBox::about(
        this, tr("About TalkInput"),
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

  // ── History table setup ──────────────────────────────────────
  m_ui->historyTable->horizontalHeader()->setStretchLastSection(false);
  m_ui->historyTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_ui->historyTable->setColumnWidth(1, 32);
  m_ui->historyTable->setColumnWidth(2, 32);
  m_ui->historyTable->verticalHeader()->hide();
  m_ui->historyTable->verticalHeader()->setDefaultSectionSize(30);

  // ── Restore persisted state ──────────────────────────────────
  QSettings s;
  const QString savedDir =
      s.value(QStringLiteral("model/directory")).toString();
  const QString savedName =
      s.value(QStringLiteral("model/name")).toString();
  if (!savedDir.isEmpty()) {
    setRecognitionModel(savedDir, savedName);
    spdlog::info("Restored model: {} ({})", savedName, savedDir);
  }

  // ── Load history ────────────────────────────────────────────
  refreshHistory();
}

void MainWindow::toggleListening() {
  spdlog::info("Toggle listening (currently {})",
               m_audioSource ? "listening" : "idle");
  if (m_audioSource)
    stopListening();
  else
    startListening();
}

void MainWindow::startListening() {
  spdlog::info("Starting listening with model: {}",
               m_currentModelDirectory);

  if (m_currentModelDirectory.isEmpty()) {
    QMessageBox::warning(this, tr("Speech recognition"),
                         tr("No model selected.\n\n"
                            "Go to the Models tab and select a model first."));
    return;
  }

  const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
  if (inputDevice.isNull()) {
    spdlog::error("No audio input device available");
    QMessageBox::warning(this, tr("Audio input"),
                         tr("No audio input device is available."));
    return;
  }

  spdlog::info("Audio input device: {}", inputDevice.description());

  m_audioFormat = inputDevice.preferredFormat();

  if (!m_audioFormat.isValid() ||
      m_audioFormat.sampleFormat() == QAudioFormat::Unknown) {
    m_audioFormat = QAudioFormat();
    m_audioFormat.setSampleRate(48000);
    m_audioFormat.setChannelCount(1);
    m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    spdlog::warn("Audio format invalid, using default: 48000 Hz, 1ch, Int16");
  }

  if (!inputDevice.isFormatSupported(m_audioFormat) ||
      m_audioFormat.sampleFormat() == QAudioFormat::Unknown) {
    spdlog::error("Audio format not supported by device");
    QMessageBox::warning(
        this, tr("Audio input"),
        tr("The default microphone does not expose a supported capture "
           "format."));
    return;
  }

  spdlog::info("Audio format: {} Hz, {} channels",
               m_audioFormat.sampleRate(), m_audioFormat.channelCount());

  SpeechRecognizer::Config config;
  config.modelDir = m_currentModelDirectory;

  QString encoderPath, decoderPath, joinerPath, tokensPath;
  if (!detectModelFiles(config.modelDir, encoderPath, decoderPath, joinerPath,
                        tokensPath)) {
    spdlog::error("Model missing required transducer files in: {}",
                  config.modelDir);
    QMessageBox::warning(
        this, tr("Speech recognition"),
        tr("The selected model does not support streaming recognition.\n\n"
           "This recognizer requires a streaming transducer model with\n"
           "encoder, decoder, joiner onnx files and tokens.txt.\n\n"
           "Please select a valid streaming model from the Models tab."));
    return;
  }

  config.encoderFile = QDir(config.modelDir).relativeFilePath(encoderPath);
  config.decoderFile = QDir(config.modelDir).relativeFilePath(decoderPath);
  config.joinerFile = QDir(config.modelDir).relativeFilePath(joinerPath);
  spdlog::info("Model files: encoder={}, decoder={}, joiner={}",
               config.encoderFile, config.decoderFile, config.joinerFile);
  config.sampleRate = m_audioFormat.sampleRate();

  m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
  m_audioDevice = m_audioSource->start();
  if (!m_audioDevice) {
    spdlog::error("Failed to start microphone capture");
    m_audioSource.reset();
    QMessageBox::warning(this, tr("Audio input"),
                         tr("Failed to start microphone capture."));
    return;
  }

  spdlog::info("Microphone capture started (suspended)");
  connect(m_audioDevice, &QIODevice::readyRead, this, &MainWindow::readAudio);
  m_audioSource->suspend();

  spdlog::info("Loading model async from: {}", config.modelDir);

  showLoadingDialog(tr("Loading model, please wait..."));
  m_loadingModel = true;
  updateControls(false);

  loadModelAsync(config);
}

void MainWindow::stopListening() {
  spdlog::info("Stopping listening");
  if (m_loadingModel) {
    spdlog::warn("Model still loading, cancelling");
    m_loadingModel = false;
    hideLoadingDialog();
    if (m_audioSource) {
      m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();
    m_recognizer.stop();
    updateControls(false);
    spdlog::info("Listening cancelled during model load");
    return;
  }
  if (m_audioSource) {
    m_audioSource->stop();
  }
  m_audioDevice = nullptr;
  m_audioSource.reset();

  if (m_recognizer.isRunning()) {
    m_recognizer.finish();
    m_recognizer.stop();
  }

  updateControls(false);
}

void MainWindow::readAudio() {
  if (!m_audioDevice)
    return;

  const QByteArray audioData = m_audioDevice->readAll();
  const QByteArray pcm16 = convertToPcm16(audioData, m_audioFormat);
  m_recognizer.acceptPcm16(pcm16, m_audioFormat.sampleRate(),
                           m_audioFormat.channelCount());
}

void MainWindow::updateControls(bool listening) {
  if (listening) {
    applyIcon(m_ui->startButton, ":/resources/stop.svg", 28);
    m_ui->startButton->setToolTip(tr("Stop recognition"));
  } else {
    applyIcon(m_ui->startButton, ":/resources/mic.svg", 28);
    m_ui->startButton->setToolTip(tr("Start recognition"));
  }
  m_ui->startButton->setEnabled(!m_loadingModel);

  if (listening)
    statusBar()->showMessage(
        m_currentModelName.isEmpty()
            ? tr("Listening...")
            : tr("Listening — %1").arg(m_currentModelName));
  else if (m_loadingModel)
    statusBar()->showMessage(tr("Loading model..."));
  else if (m_currentModelDirectory.isEmpty())
    statusBar()->showMessage(tr("No model selected"));
  else
    statusBar()->showMessage(tr("Model: %1").arg(m_currentModelName));
}

void MainWindow::setRecognitionModel(const QString &modelDirectory,
                                     const QString &modelName) {
  const QString normalizedDirectory =
      QDir::fromNativeSeparators(modelDirectory.trimmed());
  m_currentModelDirectory = normalizedDirectory.isEmpty()
                                ? QString()
                                : QDir::cleanPath(normalizedDirectory);
  m_currentModelName = modelName.trimmed();

  if (m_currentModelName.isEmpty())
    m_currentModelName = QFileInfo(m_currentModelDirectory).fileName();

  if (m_currentModelDirectory.isEmpty())
    statusBar()->showMessage(tr("No model selected"));
  else
    statusBar()->showMessage(tr("Model: %1").arg(m_currentModelName));
  spdlog::info("Recognition model set: {} ({})",
               m_currentModelName, m_currentModelDirectory);

  QSettings s;
  s.setValue(QStringLiteral("model/directory"), m_currentModelDirectory);
  s.setValue(QStringLiteral("model/name"), m_currentModelName);
}

void MainWindow::showLoadingDialog(const QString &message) {
  if (m_loadingDialog)
    return;

  m_loadingDialog = new QDialog(this);
  m_loadingDialog->setWindowTitle(tr("Loading Model"));
  m_loadingDialog->setWindowModality(Qt::WindowModal);
  m_loadingDialog->setFixedSize(320, 160);
  m_loadingDialog->setWindowFlags(
      (m_loadingDialog->windowFlags() | Qt::Dialog) &
      ~Qt::WindowCloseButtonHint);

  auto *layout = new QVBoxLayout(m_loadingDialog);
  layout->setAlignment(Qt::AlignCenter);
  layout->setSpacing(16);

  auto *spinner = new QLabel(m_loadingDialog);
  spinner->setAlignment(Qt::AlignCenter);
  spinner->setFixedSize(48, 48);
  spinner->setText(QStringLiteral("\u23F3"));
  QFont spinnerFont = spinner->font();
  spinnerFont.setPointSize(32);
  spinner->setFont(spinnerFont);
  layout->addWidget(spinner);

  auto *textLabel = new QLabel(message, m_loadingDialog);
  textLabel->setAlignment(Qt::AlignCenter);
  textLabel->setWordWrap(true);
  QFont font = textLabel->font();
  font.setPointSize(12);
  textLabel->setFont(font);
  layout->addWidget(textLabel);

  m_loadingDialog->show();
  QCoreApplication::processEvents();
}

void MainWindow::hideLoadingDialog() {
  if (m_loadingDialog) {
    m_loadingDialog->close();
    m_loadingDialog->deleteLater();
    m_loadingDialog = nullptr;
  }
}

void MainWindow::loadModelAsync(const SpeechRecognizer::Config &config) {
  m_modelLoadFuture = QtConcurrent::run([this, config]() -> bool {
    QString errorMessage;
    return m_recognizer.start(config, &errorMessage);
  });

  m_modelLoadWatcher.setFuture(m_modelLoadFuture);
}

void MainWindow::onModelLoaded() {
  hideLoadingDialog();

  if (!m_loadingModel) {
    spdlog::warn("Model load completed but listening was cancelled");
    if (m_recognizer.isRunning())
      m_recognizer.stop();
    return;
  }

  m_loadingModel = false;
  const bool success = m_modelLoadFuture.result();

  if (!success) {
    spdlog::error("Model loading failed");
    if (m_audioSource) {
      m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();
    m_recognizer.stop();
    QMessageBox::warning(this, tr("Speech recognition"),
                         tr("Failed to load model. Please check the model "
                            "files are valid and complete."));
    updateControls(false);
    return;
  }

  spdlog::info("Model loaded successfully, starting audio capture");
  if (m_audioSource) {
    m_audioSource->resume();
  }

  updateControls(true);
}

void MainWindow::onResult(const QString &text, bool isFinal) {
  SPDLOG_LOGGER_CALL(
      spdlog::default_logger_raw(), spdlog::level::info,
      "{} {}", isFinal ? "[final]" : "[partial]", text);

  if (isFinal && !text.trimmed().isEmpty()) {
    m_history.addEntry(text.trimmed());
    refreshHistory();
  }

  if (auto *sb = statusBar()) {
    if (isFinal)
      sb->showMessage(
          m_currentModelName.isEmpty()
              ? tr("Listening...")
              : tr("Listening — %1").arg(m_currentModelName));
  }
}

void MainWindow::refreshHistory() {
  const auto entries = m_history.allEntries();

  m_ui->historyTable->setUpdatesEnabled(false);
  m_ui->historyTable->setRowCount(entries.size());

  for (int i = 0; i < entries.size(); ++i) {
    const auto &e = entries.at(i);

    // Truncate to ~55 chars for display
    QString display = e.text;
    if (display.length() > 55)
      display = display.left(55) + QStringLiteral("...");
    auto *textItem = new QTableWidgetItem(display);
    textItem->setData(Qt::UserRole, e.id);
    textItem->setToolTip(e.text);
    textItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_ui->historyTable->setItem(i, 0, textItem);

    // Copy button
    auto *copyBtn = new QPushButton();
    copyBtn->setIcon(QIcon(QStringLiteral(":/resources/copy.svg")));
    copyBtn->setIconSize(QSize(18, 18));
    copyBtn->setToolTip(tr("Copy text"));
    copyBtn->setFlat(true);
    connect(copyBtn, &QPushButton::clicked, this, [this, i]() {
      copyEntry(i);
    });

    // Delete button
    auto *delBtn = new QPushButton();
    delBtn->setIcon(QIcon(QStringLiteral(":/resources/delete.svg")));
    delBtn->setIconSize(QSize(18, 18));
    delBtn->setToolTip(tr("Delete entry"));
    delBtn->setFlat(true);
    connect(delBtn, &QPushButton::clicked, this, [this, i]() {
      deleteEntry(i);
    });

    auto *container = new QWidget();
    auto *lay = new QHBoxLayout(container);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addStretch();
    lay->addWidget(copyBtn);
    lay->addWidget(delBtn);
    m_ui->historyTable->setCellWidget(i, 1, copyBtn);
    m_ui->historyTable->setCellWidget(i, 2, delBtn);
  }

  m_ui->historyTable->setUpdatesEnabled(true);
}

void MainWindow::copyEntry(int row) {
  auto *item = m_ui->historyTable->item(row, 0);
  if (!item) return;

  const auto entries = m_history.allEntries();
  if (row >= 0 && row < entries.size()) {
    QApplication::clipboard()->setText(entries.at(row).text);
    statusBar()->showMessage(tr("Copied."), 2000);
  }
}

void MainWindow::deleteEntry(int row) {
  auto *item = m_ui->historyTable->item(row, 0);
  if (!item) return;

  const int id = item->data(Qt::UserRole).toInt();
  m_history.deleteEntry(id);
  refreshHistory();
  statusBar()->showMessage(tr("Deleted."), 2000);
}

} // namespace talkinput
