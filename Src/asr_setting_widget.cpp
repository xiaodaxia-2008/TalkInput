#include "asr_setting_widget.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_registry.h"
#include "utils.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>

#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

QString cacheDir()
{
    QString base =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty()) {
        base = QDir::current().filePath(QStringLiteral("cache"));
    }
    return QDir(base).filePath(QStringLiteral("models"));
}

QString displayTypeForPreset(const talkinput::ModelPreset &preset)
{
    if (preset.typeStr == "Tool") {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "Tool");
    }
    if (preset.streamingSupport) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "Online");
    }
    if (preset.typeStr == "FunASRNano" || preset.typeStr == "Qwen3ASR") {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "Offline (LLM)");
    }
    return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                       "Offline");
}

QString displayNameForPreset(const talkinput::ModelPreset &preset)
{
    return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                       preset.name.toUtf8().constData());
}

} // namespace

namespace talkinput
{

AsrSettingWidget::AsrSettingWidget(QWidget *parent)
    : QWidget(parent), m_networkManager(new QNetworkAccessManager(this))
{
    spdlog::debug("AsrSettingWidget: constructor begin");

    connect(m_networkManager, &QNetworkAccessManager::finished, this,
            &AsrSettingWidget::onDownloadFinished);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    m_table = new QTableWidget(this);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({tr("Model"), tr("Type"),
                                        tr("Languages"), tr("Size"),
                                        tr("Status"), QString()});
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 120);
    m_table->setColumnWidth(2, 100);
    m_table->setColumnWidth(3, 100);
    m_table->setColumnWidth(4, 100);
    m_table->setColumnWidth(5, 110);
    m_table->verticalHeader()->hide();
    root->addWidget(m_table);

    auto *llmGroup = new QGroupBox(tr("LLM Service"), this);
    auto *llmForm = new QFormLayout(llmGroup);
    llmForm->setContentsMargins(10, 10, 10, 10);
    llmForm->setSpacing(8);

    auto *endpointEdit = new QLineEdit(llmGroup);
    endpointEdit->setPlaceholderText(
        tr("OpenAI-compatible chat completions endpoint"));
    auto *modelEdit = new QLineEdit(llmGroup);
    modelEdit->setPlaceholderText(tr("Model name sent to the LLM service"));
    auto *apiKeyEdit = new QLineEdit(llmGroup);
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText(tr("Optional API key"));

    {
        QSettings settings;
        const QString endpoint =
            settings.value("llm/endpoint", defaultLlmEndpoint()).toString();
        const QString model =
            settings.value("llm/model", defaultLlmModel()).toString();
        endpointEdit->setText(endpoint);
        modelEdit->setText(model);
        apiKeyEdit->setText(settings.value("llm/apiKey").toString());
    }

    connect(endpointEdit, &QLineEdit::editingFinished, this,
            [this, endpointEdit]() {
                const QString endpoint = endpointEdit->text().trimmed();
                QSettings settings;
                settings.setValue("llm/endpoint", endpoint.isEmpty()
                                                      ? defaultLlmEndpoint()
                                                      : endpoint);
                endpointEdit->setText(
                    settings.value("llm/endpoint").toString());
                emit statusMessage(tr("LLM endpoint saved."));
            });
    connect(modelEdit, &QLineEdit::editingFinished, this, [this, modelEdit]() {
        const QString model = modelEdit->text().trimmed();
        QSettings settings;
        settings.setValue("llm/model",
                          model.isEmpty() ? defaultLlmModel() : model);
        modelEdit->setText(settings.value("llm/model").toString());
        emit statusMessage(tr("LLM model saved."));
    });
    connect(apiKeyEdit, &QLineEdit::editingFinished, this,
            [this, apiKeyEdit]() {
                QSettings settings;
                settings.setValue("llm/apiKey", apiKeyEdit->text().trimmed());
                emit statusMessage(tr("LLM API key saved."));
            });

    llmForm->addRow(tr("Endpoint"), endpointEdit);
    llmForm->addRow(tr("Model"), modelEdit);
    llmForm->addRow(tr("API Key"), apiKeyEdit);
    root->addWidget(llmGroup);

    auto *bottomRow = new QHBoxLayout();
    auto *archiveBtn = new QPushButton(tr("Use Archive"), this);
    archiveBtn->setToolTip(tr("Import and extract a model archive"));
    connect(archiveBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseArchive);

    auto *openBtn = new QPushButton(tr("Open Folder"), this);
    openBtn->setToolTip(tr("Open model cache directory"));
    connect(openBtn, &QPushButton::clicked, this, &AsrSettingWidget::onOpenDir);

    auto *hotwordsBtn = new QPushButton(tr("Hot Words"), this);
    hotwordsBtn->setToolTip(tr("Edit hot words"));
    connect(hotwordsBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);

    auto *llmPostProcessCheck = new QCheckBox(tr("LLM post-processing"), this);
    llmPostProcessCheck->setToolTip(
        tr("Use a local Qwen model to polish final recognition text"));
    {
        QSettings settings;
        llmPostProcessCheck->setChecked(
            settings.value("llm/postProcessingEnabled", false).toBool());
    }
    connect(
        llmPostProcessCheck, &QCheckBox::toggled, this, [this](bool checked) {
            QSettings settings;
            settings.setValue("llm/postProcessingEnabled", checked);
            emit statusMessage(checked ? tr("LLM post-processing enabled.")
                                       : tr("LLM post-processing disabled."));
        });

    bottomRow->addWidget(archiveBtn);
    bottomRow->addWidget(openBtn);
    bottomRow->addWidget(hotwordsBtn);
    bottomRow->addWidget(llmPostProcessCheck);
    bottomRow->addStretch();
    root->addLayout(bottomRow);

    // ── Model definitions ──────────────────────────────────────
    spdlog::debug("AsrSettingWidget: loading ASR presets");
    for (const auto &preset : loadModelPresets()) {
        spdlog::debug("AsrSettingWidget: ASR preset {} ({})", preset.name,
                      preset.modelDirName);
        m_models.append({displayNameForPreset(preset),
                         displayTypeForPreset(preset), preset.languages,
                         preset.modelDirName, QUrl(preset.url), preset.size,
                         preset.paramCount, preset.streamingSupport,
                         preset.isPunctuationModel});
    }

    spdlog::debug("AsrSettingWidget: loading tool presets");
    for (const auto &preset : loadToolPresets()) {
        spdlog::debug("AsrSettingWidget: tool preset {} ({})", preset.name,
                      preset.modelDirName);
        m_models.append({displayNameForPreset(preset),
                         displayTypeForPreset(preset), preset.languages,
                         preset.modelDirName, QUrl(preset.url), preset.size,
                         preset.paramCount, preset.streamingSupport,
                         preset.isPunctuationModel});
    }

    // Auto-download punctuation model after UI is ready
    m_startupTimer = new QTimer(this);
    m_startupTimer->setSingleShot(true);
    connect(m_startupTimer, &QTimer::timeout, this,
            &AsrSettingWidget::ensurePunctuationModel);
    m_startupTimer->start(1500);

    // ── Table rows ──────────────────────────────────────────────
    spdlog::debug("AsrSettingWidget: populating table with {} rows",
                  m_models.size());
    populateTable();
    spdlog::debug("AsrSettingWidget: table populated");

    // Apply icons to bottom buttons
    setButtonIcon(archiveBtn, ":/resources/folder-plus.svg", 22);
    setButtonIcon(openBtn, ":/resources/folder.svg", 22);
    setButtonIcon(hotwordsBtn, ":/resources/hotwords.svg", 22);
    archiveBtn->setProperty("buttonRole", "icon");
    openBtn->setProperty("buttonRole", "icon");
    hotwordsBtn->setProperty("buttonRole", "icon");
    spdlog::debug("AsrSettingWidget: constructor end");
}

AsrSettingWidget::~AsrSettingWidget()
{
    if (m_activeDownloadReply) {
        m_activeDownloadReply->abort();
        m_activeDownloadReply->deleteLater();
        m_activeDownloadReply = nullptr;
    }
}

// ── Table ─────────────────────────────────────────────────────

void AsrSettingWidget::populateTable()
{
    spdlog::debug("AsrSettingWidget::populateTable: begin");
    m_table->setRowCount(m_models.size());

    for (int i = 0; i < m_models.size(); ++i) {
        const auto &m = m_models.at(i);

        auto *nameItem = new QTableWidgetItem(m.name);
        nameItem->setData(Qt::UserRole, i);
        m_table->setItem(i, 0, nameItem);
        m_table->setItem(i, 1, new QTableWidgetItem(m.type));
        m_table->setItem(i, 2, new QTableWidgetItem(m.languages));

        QString szStr;
        if (m.modelSize >= 1073741824) {
            szStr = QStringLiteral("%1 GB").arg(
                static_cast<double>(m.modelSize) / 1073741824.0, 0, 'f', 1);
        }
        else {
            szStr = QStringLiteral("%1 MB").arg(
                static_cast<double>(m.modelSize) / 1048576.0, 0, 'f', 0);
        }
        m_table->setItem(i, 3, new QTableWidgetItem(szStr));

        // Action buttons container
        auto *container = new QWidget();
        auto *lay = new QHBoxLayout(container);
        lay->setContentsMargins(4, 2, 4, 2);
        lay->setSpacing(4);

        auto *useBtn = new QPushButton();
        if (m.isPunctuationModel) {
            useBtn->setEnabled(false);
            useBtn->setToolTip(tr("Auto-loaded punctuation model"));
        }
        else {
            useBtn->setToolTip(tr("Use this model"));
        }
        connect(useBtn, &QPushButton::clicked, this, [this, i]() { onUse(i); });

        auto *dlBtn = new QPushButton();
        dlBtn->setToolTip(tr("Download this model"));
        connect(dlBtn, &QPushButton::clicked, this,
                [this, i]() { onDownload(i); });

        auto *delBtn = new QPushButton();
        delBtn->setToolTip(tr("Delete this model"));
        connect(delBtn, &QPushButton::clicked, this,
                [this, i]() { onDelete(i); });

        setButtonIcon(useBtn, ":/resources/check.svg", 18);
        useBtn->setProperty("buttonRole", "icon");
        useBtn->setProperty("actionRole", "use");

        setButtonIcon(dlBtn, ":/resources/download.svg", 18);
        dlBtn->setProperty("buttonRole", "icon");
        dlBtn->setProperty("actionRole", "download");

        setButtonIcon(delBtn, ":/resources/delete.svg", 18);
        delBtn->setProperty("buttonRole", "icon");
        delBtn->setProperty("actionRole", "delete");

        lay->addWidget(useBtn);
        lay->addWidget(dlBtn);
        lay->addWidget(delBtn);
        m_table->setCellWidget(i, 5, container);
    }

    m_punctuationRow = -1;
    for (int i = 0; i < m_models.size(); ++i) {
        if (m_models[i].isPunctuationModel) {
            m_punctuationRow = i;
            break;
        }
    }

    refreshStatus();
    spdlog::debug("AsrSettingWidget::populateTable: end");
}

void AsrSettingWidget::refreshStatus()
{
    spdlog::debug("AsrSettingWidget::refreshStatus: begin");
    QSettings s;
    const QString activeDir =
        s.value(QStringLiteral("model/directory")).toString();

    for (int i = 0; i < m_models.size(); ++i) {
        const auto &m = m_models.at(i);
        const bool installed = isInstalled(i);
        const QString path = QDir(cacheDir()).filePath(m.modelDirName);
        const bool isActive =
            !activeDir.isEmpty() && QDir(activeDir) == QDir(path);

        QString statusText;
        QColor statusColor;
        if (isActive) {
            statusText = tr("Active");
            statusColor = QColor(0x15, 0x65, 0xc0);
        }
        else if (installed) {
            statusText = tr("Installed");
            statusColor = QColor(0x2e, 0x7d, 0x32);
        }
        else {
            statusText = tr("Not installed");
            statusColor = QColor(0xc6, 0x28, 0x28);
        }

        auto *st = new QTableWidgetItem(statusText);
        st->setForeground(statusColor);
        m_table->setItem(i, 4, st);

        auto *w = m_table->cellWidget(i, 5);
        if (!w) {
            continue;
        }
        auto btns = w->findChildren<QPushButton *>();
        if (btns.size() >= 3) {
            btns[0]->setEnabled(installed);  // Use
            btns[1]->setVisible(!installed); // Download
            btns[2]->setVisible(installed);  // Delete
        }
    }
    spdlog::debug("AsrSettingWidget::refreshStatus: end");
}

// ── Punctuation model helpers ─────────────────────────────────

bool AsrSettingWidget::isInstalled(int row) const
{
    if (row < 0 || row >= m_models.size()) {
        return false;
    }
    const auto &m = m_models.at(row);
    const QString path = QDir(cacheDir()).filePath(m.modelDirName);
    return QFileInfo(path).isDir();
}

QString AsrSettingWidget::punctuationModelName()
{
    return QStringLiteral(
        "sherpa-onnx-punct-ct-transformer-zh-en-vocab272727-2024-04-12-int8");
}

void AsrSettingWidget::ensurePunctuationModel()
{
    if (m_punctuationRow < 0) {
        return;
    }
    if (isInstalled(m_punctuationRow)) {
        return;
    }
    if (m_activeDownloadReply) {
        return;
    }

    spdlog::info("Punctuation model not found, starting auto-download...");
    emit statusMessage(tr("Punctuation model not found, downloading..."));
    onDownload(m_punctuationRow);
}

// ── Actions ───────────────────────────────────────────────────

void AsrSettingWidget::onUse(int row)
{
    if (row < 0 || row >= m_models.size()) {
        return;
    }
    const auto &m = m_models.at(row);
    const QString dir = QDir(cacheDir()).filePath(m.modelDirName);

    if (!QFileInfo(dir).isDir()) {
        QMessageBox::warning(this, tr("Model not found"),
                             tr("Directory does not exist:\n%1")
                                 .arg(QDir::toNativeSeparators(dir)));
        return;
    }

    if (!m.streamingSupport) {
        QMessageBox::information(
            this, tr("Offline model"),
            tr("This model does not support real-time recognition."));
    }

    spdlog::info("Selected model: {} ({})", m.name, dir);
    emit modelSelected(dir, m.name);
}

void AsrSettingWidget::onDownload(int row)
{
    if (row < 0 || row >= m_models.size() || m_activeDownloadReply) {
        return;
    }

    const auto &m = m_models.at(row);
    if (m.archiveUrl.isEmpty()) {
        return;
    }

    QDir cache(cacheDir());
    if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) {
        return;
    }

    const QString archiveName = QFileInfo(m.archiveUrl.path()).fileName();
    m_activeDownloadPath = cache.filePath(archiveName);
    m_activeDownloadTempPath = m_activeDownloadPath + QStringLiteral(".part");
    m_downloadTargetRow = row;

    QFile::remove(m_activeDownloadTempPath);
    m_activeDownloadFile = std::make_unique<QFile>(m_activeDownloadTempPath);
    if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) {
        return;
    }

    // Show download progress text in status column
    auto *progressItem = new QTableWidgetItem(tr("Downloading..."));
    progressItem->setForeground(QColor(0x15, 0x65, 0xc0));
    m_table->setItem(row, 4, progressItem);

    emit statusMessage(tr("Downloading %1...").arg(m.name));
    QNetworkRequest req(m.archiveUrl);
    m_activeDownloadReply = m_networkManager->get(req);
    connect(m_activeDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_activeDownloadReply && m_activeDownloadFile) {
            m_activeDownloadFile->write(m_activeDownloadReply->readAll());
        }
    });
    connect(m_activeDownloadReply, &QNetworkReply::downloadProgress, this,
            [this, row](qint64 received, qint64 total) {
                if (total <= 0) {
                    return;
                }
                int pct = static_cast<int>(received * 100 / total);
                auto *item =
                    new QTableWidgetItem(tr("Downloading %1%").arg(pct));
                item->setForeground(QColor(0x15, 0x65, 0xc0));
                m_table->setItem(row, 4, item);
            });
}

void AsrSettingWidget::onDelete(int row)
{
    if (row < 0 || row >= m_models.size()) {
        return;
    }
    const auto &m = m_models.at(row);
    const QString dir = QDir(cacheDir()).filePath(m.modelDirName);
    if (!QFileInfo(dir).isDir()) {
        return;
    }

    if (QMessageBox::question(this, tr("Delete model"),
                              tr("Delete \"%1\"?\n\n%2")
                                  .arg(m.name)
                                  .arg(QDir::toNativeSeparators(dir))) !=
        QMessageBox::Yes)
    {
        return;
    }

    QDir(dir).removeRecursively();
    spdlog::info("Deleted model: {} ({})", m.name, dir);
    emit statusMessage(tr("Deleted: %1").arg(m.name));
    refreshStatus();
}

void AsrSettingWidget::onUseArchive()
{
    const QString filter = tr(
        "Model Archives (*.tar.bz2 *.tar.gz *.tgz *.tar *.zip);;All Files (*)");
    const QString defaultDir =
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select model archive"), defaultDir, filter);
    if (path.isEmpty()) {
        return;
    }

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
    for (const QString &e : {".tar.bz2", ".tar.gz", ".tgz", ".tar", ".zip"}) {
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
    }
    else {
        emit statusMessage(tr("Directory not found: %1")
                               .arg(QDir::toNativeSeparators(modelDir)));
    }

    refreshStatus();
}

void AsrSettingWidget::onOpenDir()
{
    QDir dir(cacheDir());
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
}

void AsrSettingWidget::onEditHotwords()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Hot Words"));
    dialog.resize(420, 320);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *hintRow = new QHBoxLayout();
    hintRow->setSpacing(8);

    auto *iconLabel = new QLabel("💡", &dialog);
    iconLabel->setObjectName("hotwordsHintIcon");
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(20, 20);

    auto *hintLabel = new QLabel(tr("One hot word per line."), &dialog);
    hintLabel->setObjectName("hotwordsHintLabel");
    hintLabel->setWordWrap(true);

    hintRow->addWidget(iconLabel);
    hintRow->addWidget(hintLabel, 1);
    layout->addLayout(hintRow);

    auto *editor = new QTextEdit(&dialog);
    editor->setAcceptRichText(false);
    editor->setPlaceholderText(tr("Enter hot words, one per line"));
    {
        QSettings s;
        editor->setPlainText(s.value("model/hotwords").toString());
    }
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    QSettings s;
    s.setValue("model/hotwords", editor->toPlainText().trimmed());
    emit statusMessage(tr("Hot words saved."));
    emit hotwordsChanged();
}

void AsrSettingWidget::onDownloadFinished()
{
    auto *reply = m_activeDownloadReply;
    m_activeDownloadReply = nullptr;

    if (m_activeDownloadFile) {
        m_activeDownloadFile->write(reply->readAll());
        m_activeDownloadFile->close();
    }

    const bool failed = !reply || reply->error() != QNetworkReply::NoError;
    const int row = m_downloadTargetRow;
    m_downloadTargetRow = -1;

    if (reply) {
        reply->deleteLater();
    }

    auto setStatusText = [this](int r, const QString &text,
                                const QColor &color) {
        if (r < 0 || r >= m_models.size()) {
            return;
        }
        auto *item = new QTableWidgetItem(text);
        item->setForeground(color);
        m_table->setItem(r, 4, item);
    };

    if (failed) {
        QFile::remove(m_activeDownloadTempPath);
        m_activeDownloadFile.reset();
        setStatusText(row, tr("Download failed"), QColor(0xc6, 0x28, 0x28));
        emit statusMessage(tr("Download failed."));
        return;
    }

    QFile::remove(m_activeDownloadPath);
    QFile::rename(m_activeDownloadTempPath, m_activeDownloadPath);
    m_activeDownloadFile.reset();

    setStatusText(row, tr("Extracting..."), QColor(0x15, 0x65, 0xc0));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString err;
    if (!extractArchive(m_activeDownloadPath, cacheDir(), &err)) {
        setStatusText(row, tr("Extraction failed"), QColor(0xc6, 0x28, 0x28));
        emit statusMessage(tr("Extraction failed: %1").arg(err));
        return;
    }

    if (row >= 0 && row < m_models.size()) {
        const auto &m = m_models.at(row);
        const QString modelDir = QDir(cacheDir()).filePath(m.modelDirName);
        if (QFileInfo(modelDir).isDir() || isInstalled(row)) {
            if (m.isPunctuationModel) {
                emit punctuationModelReady();
            }
            else {
                emit modelSelected(modelDir, m.name);
            }
            emit statusMessage(tr("Downloaded: %1").arg(m.name));
        }
    }

    refreshStatus();
}

} // namespace talkinput
