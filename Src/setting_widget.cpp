#include "setting_widget.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>

#include <QTableWidget>
#include <QVBoxLayout>
#include <QSvgRenderer>

#include <spdlog/spdlog.h>
#include "qt_fmt.h"

namespace {

// ── Archive helpers ────────────────────────────────────────────

bool isPathInsideDir(const QString &path, const QString &dir) {
  const QString absPath = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
  const QString absDir  = QDir::cleanPath(QFileInfo(dir).absoluteFilePath());
  return absPath == absDir ||
         absPath.startsWith(absDir + QLatin1Char('/')) ||
         absPath.startsWith(absDir + QLatin1Char('\\'));
}

QString entryPath(struct archive_entry *entry) {
  const char *utf8 = archive_entry_pathname_utf8(entry);
  return utf8 ? QString::fromUtf8(utf8)
              : QString::fromLocal8Bit(archive_entry_pathname(entry));
}

bool extractArchive(const QString &archivePath,
                    const QString &destDir, QString *error) {
  QDir d(destDir);
  if (!d.exists() && !d.mkpath(QStringLiteral("."))) {
    if (error) *error = QStringLiteral("Cannot create: %1").arg(destDir);
    return false;
  }

  archive *r = archive_read_new();
  archive_read_support_filter_all(r);
  archive_read_support_format_tar(r);

  if (archive_read_open_filename(
          r, QDir::toNativeSeparators(archivePath).toLocal8Bit().constData(),
          10240) != ARCHIVE_OK) {
    if (error) *error = QString::fromLocal8Bit(archive_error_string(r));
    archive_read_free(r);
    return false;
  }

  archive_entry *entry = nullptr;
  while (archive_read_next_header(r, &entry) == ARCHIVE_OK) {
    const QString rel = QDir::cleanPath(entryPath(entry));
    if (rel.isEmpty() || rel.startsWith(QLatin1Char('/')) ||
        rel.startsWith(QStringLiteral(".."))) {
      if (error) *error = QStringLiteral("Unsafe path: %1").arg(rel);
      archive_read_free(r);
      return false;
    }

    const QString outPath = d.filePath(rel);
    if (!isPathInsideDir(outPath, d.absolutePath())) {
      if (error)
        *error = QStringLiteral("Escapes destination: %1").arg(rel);
      archive_read_free(r);
      return false;
    }

    const auto ft = archive_entry_filetype(entry);
    if (ft == AE_IFDIR) {
      QDir().mkpath(outPath);
      archive_read_data_skip(r);
      continue;
    }
    if (ft != AE_IFREG) {
      archive_read_data_skip(r);
      continue;
    }

    QDir().mkpath(QFileInfo(outPath).absolutePath());
    QFile f(outPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      if (error) *error = QStringLiteral("Cannot write: %1").arg(outPath);
      archive_read_free(r);
      return false;
    }

    const void *buf = nullptr;
    size_t sz = 0;
    la_int64_t off = 0;
    while (archive_read_data_block(r, &buf, &sz, &off) == ARCHIVE_OK) {
      Q_UNUSED(off);
      if (f.write(static_cast<const char *>(buf),
                  static_cast<qint64>(sz)) != static_cast<qint64>(sz)) {
        if (error) *error = QStringLiteral("Write error: %1").arg(outPath);
        archive_read_free(r);
        return false;
      }
    }
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }

  archive_read_free(r);
  return true;
}

QString cacheDir() {
  QString base =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (base.isEmpty())
    base = QDir::current().filePath(QStringLiteral("cache"));
  return QDir(base).filePath(QStringLiteral("models"));
}

} // namespace

namespace talkinput {

SettingWidget::SettingWidget(QWidget *parent)
    : QWidget(parent),
      m_networkManager(new QNetworkAccessManager(this)) {

  connect(m_networkManager, &QNetworkAccessManager::finished,
          this, &SettingWidget::onDownloadFinished);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  m_table = new QTableWidget(this);
  m_table->setSelectionMode(QAbstractItemView::NoSelection);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setAlternatingRowColors(true);
  m_table->setColumnCount(5);
  m_table->setHorizontalHeaderLabels(
      {tr("Model"), tr("Type"), tr("Size"), tr("Status"), QString()});
  m_table->horizontalHeader()->setStretchLastSection(false);
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->setColumnWidth(1, 120);
  m_table->setColumnWidth(2, 100);
  m_table->setColumnWidth(3, 100);
  m_table->setColumnWidth(4, 110);
  m_table->verticalHeader()->hide();
  root->addWidget(m_table);

  auto *bottomRow = new QHBoxLayout();
  auto *archiveBtn = new QPushButton(tr("Use Archive"), this);
  archiveBtn->setToolTip(tr("Import and extract a model archive"));
  connect(archiveBtn, &QPushButton::clicked, this, &SettingWidget::onUseArchive);

  auto *openBtn = new QPushButton(tr("Open Folder"), this);
  openBtn->setToolTip(tr("Open model cache directory"));
  connect(openBtn, &QPushButton::clicked, this, &SettingWidget::onOpenDir);

  bottomRow->addWidget(archiveBtn);
  bottomRow->addWidget(openBtn);
  bottomRow->addStretch();
  root->addLayout(bottomRow);

  // ── Model definitions ──────────────────────────────────────
  m_models = {
      {tr("SenseVoice multilingual int8"),
       tr("Offline"),
       QStringLiteral("sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2")),
       230 * 1024 * 1024, 40, false},

      {tr("FunASR Nano int8"),
       tr("Offline (LLM)"),
       QStringLiteral("sherpa-onnx-funasr-nano-int8-2025-12-30"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-funasr-nano-int8-2025-12-30.tar.bz2")),
       950 * 1024 * 1024, 600, false},

      {tr("Qwen3-ASR 0.6B int8"),
       tr("Offline (LLM)"),
       QStringLiteral("sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-qwen3-asr-0.6B-int8-2026-03-25.tar.bz2")),
       937 * 1024 * 1024, 600, false},

      {tr("Streaming Paraformer bilingual zh-en"),
       tr("Online"),
       QStringLiteral("sherpa-onnx-streaming-paraformer-bilingual-zh-en"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-streaming-paraformer-bilingual-zh-en.tar.bz2")),
       60 * 1024 * 1024, 0, true},

      {tr("Streaming Zipformer bilingual zh-en int8"),
       tr("Online"),
       QStringLiteral("sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
           ".tar.bz2")),
       200 * 1024 * 1024, 90, true},

      {tr("Streaming Zipformer EN 20M"),
       tr("Online"),
       QStringLiteral("sherpa-onnx-streaming-zipformer-en-20M-2023-02-17"),
       QUrl(QStringLiteral(
           "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/"
           "sherpa-onnx-streaming-zipformer-en-20M-2023-02-17.tar.bz2")),
       40 * 1024 * 1024, 20, true},
  };

  // ── Icon helper ──────────────────────────────────────────────
  // Use QSvgRenderer for reliable SVG rendering
  auto applySvg = [](QPushButton *btn, const QString &path, int sz) {
    QSvgRenderer svg(path);
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    svg.render(&p);
    p.end();
    btn->setIcon(QIcon(pm));
    btn->setIconSize(QSize(sz, sz));
    btn->setText({});
  };

  const QString btnStyle =
      "QPushButton { background: transparent; border: 1px solid #aaa; "
      "border-radius: 3px; padding: 4px; }"
      "QPushButton:hover { background: #f0f0f0; border-color: #555; }"
      "QPushButton:disabled { border-color: #ddd; }";

  // ── Table rows ──────────────────────────────────────────────
  populateTable();

  // Apply icons to bottom buttons
  applySvg(archiveBtn, QStringLiteral(":/resources/folder-plus.svg"), 22);
  applySvg(openBtn,    QStringLiteral(":/resources/folder.svg"), 22);
  archiveBtn->setStyleSheet(btnStyle);
  openBtn->setStyleSheet(btnStyle);
}

SettingWidget::~SettingWidget() {
  if (m_activeDownloadReply) {
    m_activeDownloadReply->abort();
    m_activeDownloadReply->deleteLater();
    m_activeDownloadReply = nullptr;
  }
}

// ── Table ─────────────────────────────────────────────────────

void SettingWidget::populateTable() {
  m_table->setRowCount(m_models.size());

  for (int i = 0; i < m_models.size(); ++i) {
    const auto &m = m_models.at(i);

    auto *nameItem = new QTableWidgetItem(m.name);
    nameItem->setData(Qt::UserRole, i);
    m_table->setItem(i, 0, nameItem);
    m_table->setItem(i, 1, new QTableWidgetItem(m.type));

    QString szStr;
    if (m.modelSize >= 1073741824)
      szStr = QStringLiteral("%1 GB")
                  .arg(static_cast<double>(m.modelSize) / 1073741824.0, 0,
                       'f', 1);
    else
      szStr = QStringLiteral("%1 MB")
                  .arg(static_cast<double>(m.modelSize) / 1048576.0, 0, 'f', 0);
    m_table->setItem(i, 2, new QTableWidgetItem(szStr));

    // Action buttons container
    auto *container = new QWidget();
    auto *lay = new QHBoxLayout(container);
    lay->setContentsMargins(4, 2, 4, 2);
    lay->setSpacing(4);

    auto *useBtn = new QPushButton();
    useBtn->setToolTip(tr("Use this model"));
    connect(useBtn, &QPushButton::clicked, this, [this, i]() { onUse(i); });

    auto *dlBtn = new QPushButton();
    dlBtn->setToolTip(tr("Download this model"));
    connect(dlBtn, &QPushButton::clicked, this, [this, i]() { onDownload(i); });

    auto *delBtn = new QPushButton();
    delBtn->setToolTip(tr("Delete this model"));
    connect(delBtn, &QPushButton::clicked, this, [this, i]() { onDelete(i); });

    const QString iconStyle =
        "QPushButton { background: transparent; border: 1px solid #aaa; "
        "border-radius: 3px; padding: 4px; }"
        "QPushButton:disabled { border-color: #ddd; }";

    applyIcon(useBtn, ":/resources/check.svg", 18);
    useBtn->setStyleSheet(
        iconStyle +
        "QPushButton:hover { background: #e8f5e9; border-color: #2e7d32; }");

    applyIcon(dlBtn, ":/resources/download.svg", 18);
    dlBtn->setStyleSheet(
        iconStyle +
        "QPushButton:hover { background: #e3f2fd; border-color: #1565c0; }");

    applyIcon(delBtn, ":/resources/delete.svg", 18);
    delBtn->setStyleSheet(
        iconStyle +
        "QPushButton:hover { background: #ffebee; border-color: #c62828; }");

    lay->addWidget(useBtn);
    lay->addWidget(dlBtn);
    lay->addWidget(delBtn);
    m_table->setCellWidget(i, 4, container);
  }

  refreshStatus();
}

void SettingWidget::refreshStatus() {
  for (int i = 0; i < m_models.size(); ++i) {
    const auto &m = m_models.at(i);
    const QString path = QDir(cacheDir()).filePath(m.modelDirName);
    const bool installed = QFileInfo(path).isDir();

    auto *st = new QTableWidgetItem(
        installed ? tr("Installed") : tr("Not installed"));
    st->setForeground(installed ? QColor(0x2e, 0x7d, 0x32)
                                : QColor(0xc6, 0x28, 0x28));
    m_table->setItem(i, 3, st);

    auto *w = m_table->cellWidget(i, 4);
    if (!w) continue;
    auto btns = w->findChildren<QPushButton *>();
    if (btns.size() >= 3) {
      btns[0]->setEnabled(installed);  // Use
      btns[1]->setVisible(!installed); // Download
      btns[2]->setVisible(installed);  // Delete
    }
  }
}

// ── Icon helper ───────────────────────────────────────────────

void SettingWidget::applyIcon(QPushButton *btn, const QString &svgPath,
                              int size) {
  QSvgRenderer svg(svgPath);
  QPixmap pm(size, size);
  pm.fill(Qt::transparent);
  QPainter p(&pm);
  svg.render(&p);
  p.end();
  btn->setIcon(QIcon(pm));
  btn->setIconSize(QSize(size, size));
  btn->setText({});
}

// ── Actions ───────────────────────────────────────────────────

void SettingWidget::onUse(int row) {
  if (row < 0 || row >= m_models.size()) return;
  const auto &m = m_models.at(row);
  const QString dir = QDir(cacheDir()).filePath(m.modelDirName);

  if (!QFileInfo(dir).isDir()) {
    QMessageBox::warning(this, tr("Model not found"),
                         tr("Directory does not exist:\n%1")
                             .arg(QDir::toNativeSeparators(dir)));
    return;
  }

  if (!m.streamingSupport)
    QMessageBox::information(
        this, tr("Offline model"),
        tr("This model does not support real-time recognition."));

  spdlog::info("Selected model: {} ({})", m.name, dir);
  emit modelSelected(dir, m.name);
}

void SettingWidget::onDownload(int row) {
  if (row < 0 || row >= m_models.size() || m_activeDownloadReply) return;

  const auto &m = m_models.at(row);
  if (m.archiveUrl.isEmpty()) return;

  QDir cache(cacheDir());
  if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) return;

  const QString archiveName = QFileInfo(m.archiveUrl.path()).fileName();
  m_activeDownloadPath     = cache.filePath(archiveName);
  m_activeDownloadTempPath = m_activeDownloadPath + QStringLiteral(".part");
  m_downloadTargetRow      = row;

  QFile::remove(m_activeDownloadTempPath);
  m_activeDownloadFile = std::make_unique<QFile>(m_activeDownloadTempPath);
  if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) return;

  emit statusMessage(tr("Downloading %1...").arg(m.name));
  QNetworkRequest req(m.archiveUrl);
  m_activeDownloadReply = m_networkManager->get(req);
  connect(m_activeDownloadReply, &QNetworkReply::readyRead, this, [this]() {
    if (m_activeDownloadReply && m_activeDownloadFile)
      m_activeDownloadFile->write(m_activeDownloadReply->readAll());
  });
}

void SettingWidget::onDelete(int row) {
  if (row < 0 || row >= m_models.size()) return;
  const auto &m = m_models.at(row);
  const QString dir = QDir(cacheDir()).filePath(m.modelDirName);
  if (!QFileInfo(dir).isDir()) return;

  if (QMessageBox::question(this, tr("Delete model"),
                            tr("Delete \"%1\"?\n\n%2")
                                .arg(m.name)
                                .arg(QDir::toNativeSeparators(dir))) !=
      QMessageBox::Yes)
    return;

  QDir(dir).removeRecursively();
  spdlog::info("Deleted model: {} ({})", m.name, dir);
  emit statusMessage(tr("Deleted: %1").arg(m.name));
  refreshStatus();
}

void SettingWidget::onUseArchive() {
  const QString filter =
      tr("Model Archives (*.tar.bz2 *.tar.gz *.tgz *.tar *.zip);;All Files (*)");
  const QString defaultDir =
      QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Select model archive"), defaultDir, filter);
  if (path.isEmpty()) return;

  QDir dest(cacheDir());
  if (!dest.exists() && !dest.mkpath(".")) {
    QMessageBox::warning(this, tr("Error"),
                         tr("Cannot create model directory."));
    return;
  }

  emit statusMessage(tr("Extracting..."));
  QCoreApplication::processEvents();

  QString err;
  if (!extractArchive(path, dest.absolutePath(), &err)) {
    QMessageBox::warning(this, tr("Extraction failed"),
                         tr("Failed:\n%1").arg(err));
    emit statusMessage(tr("Extraction failed."));
    return;
  }

  QString base = QFileInfo(path).fileName();
  for (const QString &e :
       {".tar.bz2", ".tar.gz", ".tgz", ".tar", ".zip"}) {
    if (base.endsWith(e, Qt::CaseInsensitive)) {
      base.chop(e.size());
      break;
    }
  }

  const QString modelDir = dest.filePath(base);
  if (QFileInfo(modelDir).isDir()) {
    spdlog::info("Extracted model: {}", modelDir);
    emit modelSelected(modelDir, base);
    emit statusMessage(
        tr("Extracted: %1").arg(QDir::toNativeSeparators(modelDir)));
  } else {
    emit statusMessage(tr("Directory not found: %1")
                           .arg(QDir::toNativeSeparators(modelDir)));
  }

  refreshStatus();
}

void SettingWidget::onOpenDir() {
  QDir dir(cacheDir());
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) return;
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void SettingWidget::onDownloadFinished() {
  auto *reply = m_activeDownloadReply;
  m_activeDownloadReply = nullptr;

  if (m_activeDownloadFile) {
    m_activeDownloadFile->write(reply->readAll());
    m_activeDownloadFile->close();
  }

  const bool failed = !reply || reply->error() != QNetworkReply::NoError;
  const int row = m_downloadTargetRow;
  m_downloadTargetRow = -1;

  if (reply) reply->deleteLater();

  if (failed) {
    QFile::remove(m_activeDownloadTempPath);
    m_activeDownloadFile.reset();
    emit statusMessage(tr("Download failed."));
    return;
  }

  QFile::remove(m_activeDownloadPath);
  QFile::rename(m_activeDownloadTempPath, m_activeDownloadPath);
  m_activeDownloadFile.reset();

  emit statusMessage(tr("Extracting..."));

  QString err;
  if (!extractArchive(m_activeDownloadPath, cacheDir(), &err)) {
    emit statusMessage(tr("Extraction failed: %1").arg(err));
    return;
  }

  if (row >= 0 && row < m_models.size()) {
    const auto &m = m_models.at(row);
    const QString modelDir = QDir(cacheDir()).filePath(m.modelDirName);
    if (QFileInfo(modelDir).isDir()) {
      emit modelSelected(modelDir, m.name);
      emit statusMessage(tr("Downloaded: %1").arg(m.name));
    }
  }

  refreshStatus();
}

} // namespace talkinput
