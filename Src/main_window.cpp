#include "main_window.h"
#include "setting_widget.h"
#include "ui_main_window.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QAudioDecoder>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLibraryInfo>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QVBoxLayout>



namespace {

void applyIcon(QPushButton *btn, const QString &svgPath, int size) {
  btn->setIcon(QIcon(svgPath));
  btn->setIconSize(QSize(size, size));
  btn->setText({});
}

} // namespace

namespace talkinput {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), m_ui(std::make_unique<Ui::MainWindow>()) {
  setupUi();
}

MainWindow::~MainWindow() {
  if (m_asrThread) {
    m_asrThread->quit();
    m_asrThread->wait(5000);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  if (m_trayIcon && m_trayIcon->isVisible()) {
    hide();
    event->ignore();
  }
}

void MainWindow::setupUi() {
  m_ui->setupUi(this);

  // ── ASR Service (persistent worker thread) ────────────────
  m_asrService = new AsrService();
  m_asrThread = new QThread(this);
  m_asrService->moveToThread(m_asrThread);
  connect(m_asrThread, &QThread::finished, m_asrService, &QObject::deleteLater);
  m_asrThread->start();

  connect(m_asrService, &AsrService::resultChanged, this,
          &MainWindow::onResult);
  connect(m_asrService, &AsrService::modelLoadResult, this,
          [this](bool success, const QString &error) {
            if (!success) {
              qCritical() << "ASR model load failed:" << error;
              statusBar()->showMessage(tr("Model load failed: %1").arg(error));
            } else {
              qInfo() << "ASR model loaded successfully";
              statusBar()->showMessage(tr("Model ready."));
            }
          });

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
  connect(m_settingWidget, &SettingWidget::punctuationModelReady,
          this, [this]() {
            if (m_currentModelDirectory.isEmpty()) return;
            qInfo() << "Punctuation model ready, reloading ASR model...";
            statusBar()->showMessage(
                tr("Punctuation ready, reloading model..."));
            QMetaObject::invokeMethod(m_asrService, "loadModel",
                                      Qt::QueuedConnection);
          });

  // ── Buttons ────────────────────────────────────────────────
  applyIcon(m_ui->startButton, ":/resources/mic.svg", 28);
  applyIcon(m_ui->fileButton, ":/resources/folder-plus.svg", 24);

  connect(m_ui->startButton, &QPushButton::clicked, this, [this]() {
    if (m_voiceInput && m_voiceInput->isListening()) {
      m_voiceInput->stopListening();
    } else {
      startListening();
    }
  });

  connect(m_ui->fileButton, &QPushButton::clicked, this, &MainWindow::onRecognizeFile);

  // ── Style ──────────────────────────────────────────────────
  m_ui->startButton->setStyleSheet(
      "QPushButton { background: transparent; border: 2px solid #999; "
      "border-radius: 24px; }"
      "QPushButton:hover { border-color: #555; background: #f5f5f5; }"
      "QPushButton:pressed { background: #e0e0e0; }");

  statusBar()->showMessage(tr("Loading model..."));
  qInfo() << "Starting ASR service";

  // ── VoiceInputController (global hotkey, overlay, text injection) ─
  m_voiceInput = new VoiceInputController(m_asrService, &m_history, this);
  qApp->installNativeEventFilter(m_voiceInput);

  connect(m_voiceInput, &VoiceInputController::listeningChanged, this,
          [this](bool listening) {
            updateControls(listening);
          });
  connect(m_voiceInput, &VoiceInputController::statusMessage, this,
          [this](const QString &msg) {
            statusBar()->showMessage(msg);
          });

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
  m_currentLanguage = langS.value(QStringLiteral("app/language"),
                                   QStringLiteral("zh")).toString();
  if (m_currentLanguage == QStringLiteral("en"))
    m_enAction->setChecked(true);
  else
    m_zhAction->setChecked(true);

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

  // ── Realtime result label (below toolbar, hidden by default) ─
  m_realtimeLabel = new QLabel(this);
  m_realtimeLabel->setWordWrap(true);
  m_realtimeLabel->setStyleSheet(
      QStringLiteral("QLabel { background: #f5f5f5; border: 1px solid #ddd; "
                     "border-radius: 6px; padding: 8px 12px; "
                     "color: #333; font-size: 13px; }"));
  m_realtimeLabel->setTextFormat(Qt::PlainText);
  m_realtimeLabel->setMinimumHeight(36);
  m_realtimeLabel->hide();
  m_ui->recognitionLayout->insertWidget(1, m_realtimeLabel);

  // ── History header row ───────────────────────────────────────
  auto *historyHeader = new QHBoxLayout();
  historyHeader->setContentsMargins(0, 4, 0, 0);

  auto *historyTitle = new QLabel(tr("Recognition History"), this);
  historyTitle->setStyleSheet(
      QStringLiteral("font-size: 13px; font-weight: bold; color: #444;"));
  historyHeader->addWidget(historyTitle);

  historyHeader->addStretch();

  auto *clearBtn = new QPushButton(tr("Clear"), this);
  clearBtn->setFlat(true);
  clearBtn->setStyleSheet(
      QStringLiteral("QPushButton { color: #c0392b; font-size: 12px; }"
                     "QPushButton:hover { text-decoration: underline; }"));
  connect(clearBtn, &QPushButton::clicked, this, [this]() {
    auto reply = QMessageBox::question(
        this, tr("Clear History"),
        tr("Are you sure you want to clear all recognition history?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply == QMessageBox::Yes) {
      m_history.clearAll();
      refreshHistory();
      statusBar()->showMessage(tr("History cleared."), 2000);
    }
  });
  historyHeader->addWidget(clearBtn);

  auto *headerWidget = new QWidget(this);
  headerWidget->setLayout(historyHeader);
  m_ui->recognitionLayout->insertWidget(2, headerWidget);

  // ── History table setup ──────────────────────────────────────
  m_ui->historyTable->horizontalHeader()->hide();
  m_ui->historyTable->horizontalHeader()->setSectionResizeMode(
      0, QHeaderView::Stretch);
  m_ui->historyTable->setColumnCount(4);
  m_ui->historyTable->setColumnWidth(1, 32);
  m_ui->historyTable->setColumnWidth(2, 32);
  m_ui->historyTable->setColumnWidth(3, 32);
  m_ui->historyTable->verticalHeader()->hide();
  m_ui->historyTable->verticalHeader()->setDefaultSectionSize(30);

  // ── Restore persisted state & load model ────────────────────
  QSettings s;
  const QString savedDir =
      s.value(QStringLiteral("model/directory")).toString();
  const QString savedName =
      s.value(QStringLiteral("model/name")).toString();
  if (!savedDir.isEmpty()) {
    setRecognitionModel(savedDir, savedName);
    qInfo() << "Restored model:" << savedName << "(" << savedDir << ")";
  }

  // ── Load history ────────────────────────────────────────────
  refreshHistory();
}

void MainWindow::setupTrayIcon() {
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
  connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

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

void MainWindow::startListening() {
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

void MainWindow::stopListening() {
  if (m_voiceInput) {
    m_voiceInput->stopListening();
  }
}

void MainWindow::updateControls(bool listening) {
  if (listening) {
    applyIcon(m_ui->startButton, ":/resources/stop.svg", 28);
    m_ui->startButton->setToolTip(tr("Stop recognition"));
    statusBar()->showMessage(
        m_currentModelName.isEmpty()
            ? tr("Listening...")
            : tr("Listening — %1").arg(m_currentModelName));
    m_realtimeLabel->setText(QString());
    m_realtimeLabel->show();
  } else {
    applyIcon(m_ui->startButton, ":/resources/mic.svg", 28);
    m_ui->startButton->setToolTip(tr("Start recognition"));
    if (m_currentModelDirectory.isEmpty())
      statusBar()->showMessage(tr("No model selected"));
    else
      statusBar()->showMessage(tr("Model: %1").arg(m_currentModelName));
    m_realtimeLabel->hide();
  }
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
    statusBar()->showMessage(tr("Loading model..."));
  qInfo() << "Recognition model set:" << m_currentModelName
                     << "(" << m_currentModelDirectory << ")";

  QSettings s;
  s.setValue(QStringLiteral("model/directory"), m_currentModelDirectory);
  s.setValue(QStringLiteral("model/name"), m_currentModelName);

  if (m_asrService) {
    m_asrService->setModelDirectory(m_currentModelDirectory);
    QMetaObject::invokeMethod(m_asrService, "loadModel",
                              Qt::QueuedConnection);
  }
}

void MainWindow::onResult(const QString &text, bool isFinal) {
  qInfo() << (isFinal ? "[final]" : "[partial]") << text;

  if (!isFinal && !text.trimmed().isEmpty()) {
    m_realtimeLabel->setText(text.trimmed());
  }

  if (isFinal && !text.trimmed().isEmpty()) {
    m_realtimeLabel->setText(text.trimmed());
    QTimer::singleShot(0, this, &MainWindow::refreshHistory);
  }

  if (auto *sb = statusBar()) {
    if (isFinal)
      sb->showMessage(
          m_currentModelName.isEmpty()
              ? tr("Listening...")
              : tr("Listening — %1").arg(m_currentModelName));
  }
}

void MainWindow::onRecognizeFile() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Select Audio File"), QString(),
      tr("Audio Files (*.wav *.mp3 *.ogg *.flac *.m4a *.aac *.opus);;All Files (*)"));
  if (path.isEmpty())
    return;

  statusBar()->showMessage(tr("Decoding audio..."));
  qInfo() << "Recognizing file:" << path;

  auto *decoder = new QAudioDecoder(this);
  QEventLoop loop;

  QByteArray allPcm;
  bool ok = false;

  connect(decoder, &QAudioDecoder::bufferReady, this, [&]() {
    const QAudioBuffer buf = decoder->read();
    if (buf.format().sampleFormat() == QAudioFormat::Int16)
      allPcm.append(reinterpret_cast<const char *>(buf.constData<int16_t>()),
                    buf.byteCount());
    else if (buf.format().sampleFormat() == QAudioFormat::Float)
      for (int i = 0; i < buf.sampleCount(); ++i) {
        const qint16 s = static_cast<qint16>(
            std::clamp(buf.constData<float>()[i], -1.0f, 1.0f) * 32767.0f);
        allPcm.append(reinterpret_cast<const char *>(&s), sizeof(s));
      }
  });

  connect(decoder, &QAudioDecoder::finished, this, [&]() {
    ok = true;
    loop.quit();
  });

  connect(decoder, static_cast<void (QAudioDecoder::*)(QAudioDecoder::Error)>(&QAudioDecoder::error), this, [&](QAudioDecoder::Error) {
    qCritical() << "Audio decoder error:" << decoder->errorString();
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

  const int sampleRate = 16000;
  if (decoder->audioFormat().sampleRate() != sampleRate) {
    qWarning() << "Sample rate mismatch: got"
                         << decoder->audioFormat().sampleRate() << "need 16000";
  }

  qInfo() << "Decoded" << allPcm.size() << "bytes of PCM16 from" << path;

  QMetaObject::invokeMethod(
      m_asrService,
      [this, allPcm, sampleRate]() {
        m_asrService->startSession();
        m_asrService->feedAudio(allPcm, sampleRate, 1);
        m_asrService->finishSession();
      },
      Qt::QueuedConnection);
  statusBar()->showMessage(tr("Recognition sent to ASR engine."), 3000);
}

void MainWindow::refreshHistory() {
  const auto entries = m_history.allEntries();

  m_ui->historyTable->setUpdatesEnabled(false);
  m_ui->historyTable->setRowCount(entries.size());

  for (int i = 0; i < entries.size(); ++i) {
    const auto &e = entries.at(i);

    QString display = e.text;
    if (display.length() > 55)
      display = display.left(55) + QStringLiteral("...");
    auto *textItem = new QTableWidgetItem(display);
    textItem->setData(Qt::UserRole, e.id);
    textItem->setToolTip(e.text);
    textItem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_ui->historyTable->setItem(i, 0, textItem);

    auto *editBtn = new QPushButton();
    editBtn->setIcon(QIcon(QStringLiteral(":/resources/edit.svg")));
    editBtn->setIconSize(QSize(18, 18));
    editBtn->setToolTip(tr("Edit text"));
    editBtn->setFlat(true);
    connect(editBtn, &QPushButton::clicked, this, [this, i]() {
      editEntry(i);
    });

    auto *copyBtn = new QPushButton();
    copyBtn->setIcon(QIcon(QStringLiteral(":/resources/copy.svg")));
    copyBtn->setIconSize(QSize(18, 18));
    copyBtn->setToolTip(tr("Copy text"));
    copyBtn->setFlat(true);
    connect(copyBtn, &QPushButton::clicked, this, [this, i]() {
      copyEntry(i);
    });

    auto *delBtn = new QPushButton();
    delBtn->setIcon(QIcon(QStringLiteral(":/resources/delete.svg")));
    delBtn->setIconSize(QSize(18, 18));
    delBtn->setToolTip(tr("Delete entry"));
    delBtn->setFlat(true);
    connect(delBtn, &QPushButton::clicked, this, [this, i]() {
      deleteEntry(i);
    });

    m_ui->historyTable->setCellWidget(i, 1, editBtn);
    m_ui->historyTable->setCellWidget(i, 2, copyBtn);
    m_ui->historyTable->setCellWidget(i, 3, delBtn);
  }

  m_ui->historyTable->setUpdatesEnabled(true);
}

void MainWindow::editEntry(int row) {
  auto *item = m_ui->historyTable->item(row, 0);
  if (!item) return;

  const auto entries = m_history.allEntries();
  if (row < 0 || row >= entries.size()) return;

  const auto &e = entries.at(row);

  QDialog dlg(this);
  dlg.setWindowTitle(tr("Edit Recognition Text"));
  dlg.setMinimumSize(480, 260);

  auto *lay = new QVBoxLayout(&dlg);

  auto *editor = new QTextEdit(&dlg);
  editor->setPlainText(e.text);
  editor->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
  editor->selectAll();
  lay->addWidget(editor);

  auto *btns = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
  connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
  lay->addWidget(btns);

  if (dlg.exec() != QDialog::Accepted) return;

  const QString newText = editor->toPlainText().trimmed();
  if (newText.isEmpty() || newText == e.text) return;

  m_history.updateEntry(e.id, newText);
  refreshHistory();
  statusBar()->showMessage(tr("Updated."), 2000);
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

void MainWindow::retranslateUi() {
  m_ui->retranslateUi(this);

  m_prefMenu->setTitle(tr("Preferences"));
  m_langMenu->setTitle(tr("Language"));
  m_zhAction->setText(tr("Chinese"));
  m_enAction->setText(tr("English"));
  m_startHiddenAction->setText(tr("Start minimized"));
  m_helpMenu->setTitle(tr("Help"));

  statusBar()->showMessage(
      m_currentModelDirectory.isEmpty()
          ? tr("No model selected")
          : tr("Model: %1").arg(m_currentModelName));
}

void MainWindow::doSwitchLanguage(const QString &lang) {
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
    } else {
      delete appT;
    }

    auto *qtT = new QTranslator(this);
    if (qtT->load(QStringLiteral("qt_zh_CN"),
                  QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
      m_qtTranslator = qtT;
      QApplication::installTranslator(m_qtTranslator);
    } else {
      delete qtT;
    }
  }

  retranslateUi();
}

} // namespace talkinput
