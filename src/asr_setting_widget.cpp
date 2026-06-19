#include "asr_setting_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "ui_asr_setting_widget.h"
#include "utils.h"
#include "voice_input_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QtGlobal>

namespace
{

QString qs(const std::string &value)
{
    return QString::fromStdString(value);
}

QString cacheDir()
{
    return QDir(talkinput::appDataDir()).filePath(QStringLiteral("models"));
}

const nlohmann::json llmProvidersJson()
{
    return talkinput::appConfigValue("/llmPresets");
}

nlohmann::json firstLlmProviderJson()
{
    const nlohmann::json providers = llmProvidersJson();
    if (providers.is_object() && !providers.empty()) {
        return providers.begin().value();
    }
    return nlohmann::json::object();
}

nlohmann::json findLlmProviderJson(const QString &id)
{
    const nlohmann::json providers = llmProvidersJson();
    if (providers.is_object()) {
        return providers.value(id.toStdString(), nlohmann::json::object());
    }
    return nlohmann::json::object();
}

std::string llmProviderModelKey(const QString &providerId)
{
    return QStringLiteral("/settings/llm/providerModels/%1")
        .arg(providerId)
        .toStdString();
}

QString asrPresetPointer(const QString &id)
{
    return QStringLiteral("/asrPresets/%1").arg(id);
}

nlohmann::json modelJsonAtPointer(const QString &pointer)
{
    if (pointer.isEmpty()) {
        return nlohmann::json::object();
    }
    return talkinput::appConfigValue(pointer.toStdString());
}

QString modelJsonString(const nlohmann::json &obj, const std::string &key)
{
    return jsonString(obj, key);
}

bool modelJsonBool(const nlohmann::json &obj, const std::string &key)
{
    return obj.value(key, false);
}

qint64 modelJsonInt64(const nlohmann::json &obj, const std::string &key)
{
    return obj.value(key, qint64(0));
}

QUrl modelArchiveUrl(const nlohmann::json &model)
{
    const QString url = modelJsonString(model, "url");
    return url.isEmpty() ? QUrl() : QUrl(url);
}

QString formatSize(qint64 bytes)
{
    if (bytes >= 1'000'000'000) {
        return QStringLiteral("%1 GB").arg(bytes / 1'000'000'000.0, 0, 'f', 1);
    }
    if (bytes >= 1'000'000) {
        return QStringLiteral("%1 MB").arg(bytes / 1'000'000.0, 0, 'f', 1);
    }
    if (bytes >= 1'000) {
        return QStringLiteral("%1 KB").arg(bytes / 1'000.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString streamingLabel(bool streaming)
{
    return streaming
               ? QCoreApplication::translate("AsrSettingWidget", "Real-time")
               : QCoreApplication::translate("AsrSettingWidget", "Offline");
}

QString languageDisplay(const QString &code)
{
    if (code == QStringLiteral("zh"))
        return QStringLiteral("CN");
    if (code == QStringLiteral("en"))
        return QStringLiteral("EN");
    if (code == QStringLiteral("zh,en"))
        return QStringLiteral("CN/EN");
    if (code == QStringLiteral("multilingual"))
        return QCoreApplication::translate("AsrSettingWidget", "Multi");
    if (code == QStringLiteral("system"))
        return QCoreApplication::translate("AsrSettingWidget", "System");
    return code;
}

bool shouldShowAsrPreset(const nlohmann::json &preset)
{
    Q_UNUSED(preset);
    return true;
}

void setButtonIcon(QPushButton *button, const QString &iconPath, int size)
{
    if (!button) return;
    QIcon icon(iconPath);
    button->setIcon(icon);
    button->setIconSize(QSize(size, size));
}

} // namespace

namespace talkinput
{

AsrSettingWidget::AsrSettingWidget(QWidget *parent)
    : QWidget(parent),
      m_ui(std::make_unique<Ui::AsrSettingWidget>())
{
    m_ui->setupUi(this);

    // ── LLM providers combo ─────────────────────────────────────
    const nlohmann::json llmProviders = llmProvidersJson();
    auto *providerCombo = m_ui->providerCombo;
    for (const auto &[key, provider] : llmProviders.items()) {
        if (!provider.is_object()) continue;
        providerCombo->addItem(qs(provider.value("name", std::string())),
                               qs(provider.value("id", std::string())));
    }

    auto *endpointEdit = m_ui->endpointEdit;
    auto *modelCombo = m_ui->llmModelCombo;
    modelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));
    auto *apiKeyEdit = m_ui->apiKeyEdit;

    auto providerAt = [providerCombo](int index) -> nlohmann::json {
        if (index >= 0 && index < providerCombo->count()) {
            const QString id = providerCombo->itemData(index).toString();
            nlohmann::json p = findLlmProviderJson(id);
            if (p.is_object() && !p.empty()) return p;
        }
        return firstLlmProviderJson();
    };

    auto applyProvider = [providerCombo, endpointEdit, modelCombo](
                             const nlohmann::json &provider, bool persist) {
        const QString endpoint =
            qs(provider.value("endpoint", std::string())).trimmed();
        const QString currentModel =
            qs(provider.value("currentModel", std::string())).trimmed();
        {
            const QSignalBlocker epBlocker(endpointEdit);
            const QSignalBlocker mBlocker(modelCombo);
            endpointEdit->setText(endpoint);
            modelCombo->clear();
            const nlohmann::json models =
                provider.value("models", nlohmann::json::array());
            for (const auto &m : models) {
                if (m.is_string()) modelCombo->addItem(qs(m.get<std::string>()));
            }
            if (!currentModel.isEmpty() &&
                modelCombo->findText(currentModel) < 0)
            {
                modelCombo->addItem(currentModel);
            }
            modelCombo->setEditText(currentModel);
        }
        if (!persist) return;
        setAppConfigValue("/settings/llm/providerId",
                          provider.value("id", std::string()));
        const QString id = providerCombo->currentData().toString();
        if (!id.isEmpty()) {
            const QString prefix = QStringLiteral("/llmPresets/%1").arg(id);
            setAppConfigValue((prefix + "/endpoint").toStdString(), endpoint);
            setAppConfigValue((prefix + "/currentModel").toStdString(),
                              currentModel);
        }
    };

    connect(providerCombo, &QComboBox::currentIndexChanged, this,
            [this, providerCombo, providerAt, applyProvider, apiKeyEdit](
                int index) {
                const auto p = providerAt(index);
                applyProvider(p, true);
                const QString id = providerCombo->itemData(index).toString();
                if (!id.isEmpty()) {
                    const auto preset = talkinput::appConfigValue(
                        QStringLiteral("/llmPresets/%1").arg(id)
                            .toStdString());
                    apiKeyEdit->setText(
                        qs(preset.value("apiKey", std::string())));
                }
                spdlog::get("statusbar")
                    ->info("{}", tr("LLM provider saved: %1")
                                     .arg(providerCombo->itemText(index)));
            });

    connect(endpointEdit, &QLineEdit::editingFinished, this,
            [this, providerCombo]() {
                const QString id = providerCombo->currentData().toString();
                if (!id.isEmpty()) {
                    setAppConfigValue(
                        QStringLiteral("/llmPresets/%1/endpoint").arg(id)
                            .toStdString(),
                        m_ui->endpointEdit->text().trimmed());
                }
                spdlog::get("statusbar")->info("{}", tr("LLM endpoint saved"));
            });

    auto saveModel = [this, providerCombo, modelCombo]() {
        const QString id = providerCombo->currentData().toString();
        if (!id.isEmpty()) {
            setAppConfigValue(
                QStringLiteral("/llmPresets/%1/currentModel").arg(id)
                    .toStdString(),
                modelCombo->currentText().trimmed());
        }
        spdlog::get("statusbar")->info("{}", tr("LLM model saved"));
    };
    connect(modelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(modelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });
    connect(modelCombo, &QComboBox::currentTextChanged, this,
            [providerCombo, modelCombo]() {
                const QString id = providerCombo->currentData().toString();
                if (!id.isEmpty()) {
                    setAppConfigValue(
                        QStringLiteral("/llmPresets/%1/currentModel")
                            .arg(id)
                            .toStdString(),
                        modelCombo->currentText().trimmed());
                }
            });
    connect(apiKeyEdit, &QLineEdit::editingFinished, this,
            [this, providerCombo]() {
                const QString id = providerCombo->currentData().toString();
                if (!id.isEmpty()) {
                    setAppConfigValue(
                        QStringLiteral("/llmPresets/%1/apiKey").arg(id)
                            .toStdString(),
                        m_ui->apiKeyEdit->text().trimmed());
                }
                spdlog::get("statusbar")->info("{}", tr("LLM API key saved"));
            });

    // Restore saved LLM provider
    const QString savedLlmId =
        talkinput::appConfigString("/settings/llm/providerId");
    const int llmIdx = providerCombo->findData(savedLlmId);
    if (llmIdx >= 0) {
        providerCombo->setCurrentIndex(llmIdx);
    }
    else {
        providerCombo->setCurrentIndex(0);
    }
    const auto savedProvider = findLlmProviderJson(
        providerCombo->currentData().toString());
    if (savedProvider.is_object() && !savedProvider.empty()) {
        applyProvider(savedProvider, false);
        apiKeyEdit->setText(
            qs(savedProvider.value("apiKey", std::string())));
    }

    // ── Prompt edit button ──────────────────────────────────────
    auto *promptBtn = m_ui->promptEditButton;
    connect(promptBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditPrompt);
    {
        const QString usr = talkinput::appConfigString("/settings/llm/userPrompt");
        m_ui->promptLabel->setText(
            QString("%1 \342\200\246").arg(usr.simplified().left(50)));
        m_ui->promptLabel->setToolTip(usr);
    }

    // ── OCR provider combo ───────────────────────────────────────
    auto *ocrCombo = m_ui->ocrCombo;
    const nlohmann::json ocrPresets =
        talkinput::appConfigValue("/ocrPresets");
    if (ocrPresets.is_object()) {
        for (const auto &[key, preset] : ocrPresets.items()) {
            if (!preset.is_object()) continue;
            ocrCombo->addItem(qs(preset.value("name", std::string())),
                              qs(preset.value("id", std::string())));
        }
    }
    const QString savedOcrId =
        talkinput::appConfigString("/settings/ocr/providerId");
    int ocrIdx = ocrCombo->findData(savedOcrId);
    if (ocrIdx >= 0) ocrCombo->setCurrentIndex(ocrIdx);
    connect(ocrCombo, &QComboBox::currentIndexChanged, this,
            [ocrCombo](int) {
                setAppConfigValue("/settings/ocr/providerId",
                                  ocrCombo->currentData().toString());
            });

    // ── Hotwords button ──────────────────────────────────────────
    auto *hotwordsBtn = m_ui->hotwordsButton;
    connect(hotwordsBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);

    // ── LLM Polish / OCR Context checkboxes ──────────────────────
    auto *llmCheck = m_ui->llmPostProcessCheck;
    auto *ocrCheck = m_ui->ocrContextCheck;

    llmCheck->setChecked(
        appConfigBool("/settings/llm/llmPostProcessEnableForAsr", false));
    connect(llmCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAppConfigValue("/settings/llm/llmPostProcessEnableForAsr", checked);
    });

    ocrCheck->setEnabled(llmCheck->isChecked());
    ocrCheck->setChecked(
        appConfigBool("/settings/ocr/ocrContextEnableForAsr", false));
    connect(ocrCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAppConfigValue("/settings/ocr/ocrContextEnableForAsr", checked);
    });
    connect(llmCheck, &QCheckBox::toggled, ocrCheck,
            &QCheckBox::setEnabled);

    // ── ASR model combo ──────────────────────────────────────────
    const nlohmann::json asrPresets = talkinput::appConfigValue("/asrPresets");
    m_ui->modelCombo->clear();
    if (asrPresets.is_object()) {
        for (const auto &[key, p] : asrPresets.items()) {
            if (!p.is_object() || !shouldShowAsrPreset(p)) continue;
            const QString name = modelJsonString(p, "name");
            const QString label =
                QStringLiteral("%1 - %2 - %3")
                    .arg(name,
                         streamingLabel(
                             modelJsonBool(p, "streamingSupport")),
                         languageDisplay(modelJsonString(p, "languages")));
            m_ui->modelCombo->addItem(label, asrPresetPointer(
                                                QString::fromStdString(key)));
        }
    }

    // Restore saved selection
    m_activeAsrId = talkinput::appConfigString("/settings/asr/providerId");
    const QString savedAsrId = m_activeAsrId;
    int restoreIndex = -1;
    if (!savedAsrId.isEmpty()) {
        for (int ci = 0; ci < m_ui->modelCombo->count(); ++ci) {
            const nlohmann::json m =
                modelJsonAtPointer(m_ui->modelCombo->itemData(ci).toString());
            if (modelJsonString(m, "id") == savedAsrId) {
                restoreIndex = ci;
                break;
            }
        }
    }
    if (restoreIndex >= 0) {
        m_ui->modelCombo->setCurrentIndex(restoreIndex);
    }

    connect(m_ui->modelCombo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onModelChanged);

    // "Use" button
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseCurrent);

    if (m_ui->modelCombo->count() > 0) {
        onModelChanged(m_ui->modelCombo->currentIndex());
    }

    // ── Bottom icons ─────────────────────────────────────────────
    setButtonIcon(hotwordsBtn, ":/resources/icons/hotwords.svg", 22);
    hotwordsBtn->setProperty("buttonRole", "icon");
    setButtonIcon(promptBtn, ":/resources/icons/edit.svg", 22);
    promptBtn->setProperty("buttonRole", "icon");

    // ── Network download ─────────────────────────────────────────
    m_networkManager = new QNetworkAccessManager(this);
}

AsrSettingWidget::~AsrSettingWidget() = default;

void AsrSettingWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_ui->retranslateUi(this);
        refreshStatus();
    }
}

// ── Model selection ──────────────────────────────────────────────

void AsrSettingWidget::onModelChanged(int index)
{
    if (index < 0 || index >= m_ui->modelCombo->count()) return;

    const nlohmann::json m =
        modelJsonAtPointer(m_ui->modelCombo->itemData(index).toString());
    if (!m.is_object()) return;

    const QString modelName = modelJsonString(m, "name");
    const QString modelId = modelJsonString(m, "id");
    const bool isActive = (modelId == m_activeAsrId);

    // Combo item label: add "(Activated)" suffix for loaded model
    // Store the original base label in item data to allow clean toggling
    const QString baseLabel = QStringLiteral("%1 - %2 - %3")
        .arg(modelName, streamingLabel(modelJsonBool(m, "streamingSupport")),
             languageDisplay(modelJsonString(m, "languages")));
    m_ui->modelCombo->setItemText(index, isActive
                                      ? baseLabel +
                                            QStringLiteral(" (%1)")
                                                .arg(QCoreApplication::translate(
                                                    "AsrSettingWidget", "Activated"))
                                      : baseLabel);

    // Use button
    m_ui->useButton->setEnabled(!isActive);
}

void AsrSettingWidget::refreshStatus()
{
    if (m_ui->modelCombo->count() > 0) {
        onModelChanged(m_ui->modelCombo->currentIndex());
    }
}

QString AsrSettingWidget::currentModelPointer() const
{
    const int ci = m_ui->modelCombo->currentIndex();
    if (ci < 0) return {};
    return m_ui->modelCombo->currentData().toString();
}

// ── Use button (download + load) ─────────────────────────────────

void AsrSettingWidget::onUseCurrent()
{
    const QString ptr = currentModelPointer();
    const nlohmann::json m = modelJsonAtPointer(ptr);
    if (!m.is_object()) return;

    const QString modelId = modelJsonString(m, "id");
    const bool isSystem = modelJsonString(m, "type") == QStringLiteral("System");

    if (!isSystem && !isInstalled(m)) {
        // Download first, then load on completion
        const QUrl archiveUrl = modelArchiveUrl(m);
        if (archiveUrl.isEmpty()) return;

        QDir cache(cacheDir());
        if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) return;

        m_downloadTargetPointer = ptr;
        m_downloadQueue.clear();
        m_downloadQueue.enqueue(ptr);

        // Also queue punctuation model if configured
        const nlohmann::json punct =
            m.value("postPunctuationModel", nlohmann::json::object());
        if (punct.is_object() && !punct.empty() && !isInstalled(punct) &&
            !modelArchiveUrl(punct).isEmpty())
        {
            m_downloadQueue.enqueue(ptr + QStringLiteral("/postPunctuationModel"));
        }

        m_activeAsrId = modelId; // will load after download
        spdlog::get("statusbar")->info("{}", tr("Downloading %1...").arg(
                                                 modelJsonString(m, "name")));
        startModelDownload(m_downloadQueue.dequeue());
        return;
    }

    // Already installed — load directly
    auto *vc = VoiceInputController::instance();
    if (vc) vc->loadModel(m);
    m_activeAsrId = modelId;
    refreshStatus();
    spdlog::get("statusbar")->info("{}", tr("Model loaded: %1").arg(
                                             modelJsonString(m, "name")));
}

// ── Download ─────────────────────────────────────────────────────

bool AsrSettingWidget::isInstalled(const nlohmann::json &model) const
{
    if (!model.is_object()) return false;
    const QString type = modelJsonString(model, "type");
    if (type.isEmpty() || type == QStringLiteral("System")) return true;
    const QString dirName = modelJsonString(model, "modelDirName");
    if (dirName.isEmpty()) return false;
    return QFileInfo(QDir(cacheDir()).filePath(dirName)).isDir();
}

void AsrSettingWidget::startModelDownload(const QString &modelPointer)
{
    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    if (!m.is_object()) return;
    const QUrl archiveUrl = modelArchiveUrl(m);
    const QString modelName = modelJsonString(m, "name");

    const QString archiveName = QFileInfo(archiveUrl.path()).fileName();
    m_activeDownloadPath = QDir(cacheDir()).filePath(archiveName);
    m_activeDownloadTempPath = m_activeDownloadPath + QStringLiteral(".part");
    m_downloadTargetPointer = modelPointer;

    QFile::remove(m_activeDownloadTempPath);
    m_activeDownloadFile = std::make_unique<QFile>(m_activeDownloadTempPath);
    if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) {
        m_downloadTargetPointer.clear();
        spdlog::get("statusbar")->info("{}", tr("Failed to download %1").arg(modelName));
        return;
    }

    QNetworkRequest request(archiveUrl);
    m_activeDownloadReply = m_networkManager->get(request);
    connect(m_activeDownloadReply, &QNetworkReply::finished, this,
            &AsrSettingWidget::onDownloadFinished);
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
    const QString modelPointer = m_downloadTargetPointer;
    m_downloadTargetPointer.clear();

    if (reply) reply->deleteLater();

    if (failed) {
        spdlog::get("statusbar")->info("{}", tr("Download failed"));
        m_downloadQueue.clear();
        return;
    }

    // Extract archive
    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    const QString modelName = modelJsonString(m, "name");

    QDir dest(cacheDir());
    if (!dest.exists() && !dest.mkpath(QStringLiteral("."))) {
        spdlog::get("statusbar")->info("{}", tr("Extraction failed"));
        return;
    }

    spdlog::get("statusbar")->info("{}", tr("Extracting..."));
    QCoreApplication::processEvents();

    auto extractResult = extractArchive(m_activeDownloadPath, dest.absolutePath());
    if (!extractResult) {
        QMessageBox::warning(this, tr("Extraction failed"),
                             tr("Failed:\n%1").arg(extractResult.error()));
        spdlog::get("statusbar")->info("{}", tr("Extraction failed"));
        return;
    }

    QFile::remove(m_activeDownloadPath);

    // Next in queue?
    if (!m_downloadQueue.isEmpty()) {
        startModelDownload(m_downloadQueue.dequeue());
        return;
    }

    refreshStatus();

    // If the "Use" triggered this download, load the model now
    const QString modelId = modelJsonString(m, "id");
    if (m_activeAsrId == modelId) {
        auto *vc = VoiceInputController::instance();
        if (vc) vc->loadModel(m);
        spdlog::get("statusbar")->info("{}", tr("Model loaded: %1").arg(modelName));
    }
    else {
        spdlog::get("statusbar")->info("{}", tr("Downloaded: %1").arg(modelName));
    }
}

// ── Prompt edit dialog ──────────────────────────────────────────

void AsrSettingWidget::onEditPrompt()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("User Prompt"));
    dialog.resize(580, 360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    const QString hint = tr("Available variables: {{input}}, {{context}}, {{hotwords}}");
    auto *label = new QLabel(QString("<b>%1</b><br><small>%2</small>")
                                 .arg(tr("User Prompt"), hint), &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *editor = new QTextEdit(&dialog);
    editor->setAcceptRichText(false);
    editor->setPlaceholderText(
        tr("Use {{input}}, {{context}}, and {{hotwords}} as needed"));
    editor->setPlainText(talkinput::appConfigString("/settings/llm/userPrompt"));
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    const QString text = editor->toPlainText().trimmed();
    setAppConfigValue("/settings/llm/userPrompt", text);
    m_ui->promptLabel->setText(
        QString("%1 \342\200\246").arg(text.simplified().left(50)));
    m_ui->promptLabel->setToolTip(text);
    spdlog::get("statusbar")->info("{}", tr("LLM prompt saved"));
}

// ── Hotwords dialog ──────────────────────────────────────────────

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
    auto *iconLabel = new QLabel("\360\237\222\241", &dialog);
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
        const nlohmann::json hw =
            talkinput::appConfigValue("/settings/hotwords");
        QStringList lines;
        if (hw.is_array()) {
            for (const auto &item : hw) {
                if (item.is_string()) {
                    const QString s =
                        QString::fromStdString(item.get<std::string>())
                            .trimmed();
                    if (!s.isEmpty()) lines.append(s);
                }
            }
        }
        editor->setPlainText(lines.join(QLatin1Char('\n')));
    }
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return;

    nlohmann::json arr = nlohmann::json::array();
    const QStringList lines =
        editor->toPlainText().trimmed().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            arr.push_back(trimmed.toStdString());
        }
    }
    setAppConfigValue("/settings/hotwords", std::move(arr));
    spdlog::get("statusbar")->info("{}", tr("Hot words saved"));
    emit hotwordsChanged();
}

} // namespace talkinput
