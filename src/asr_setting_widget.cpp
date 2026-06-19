#include "asr_setting_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "ui_asr_setting_widget.h"
#include "utils.h"

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
    if (providers.is_array() && !providers.empty()) {
        return providers.front();
    }
    return nlohmann::json::object();
}

QString formatSize(qint64 bytes)
{
    if (bytes >= 1073741824) {
        return QStringLiteral("%1 GB").arg(
            static_cast<double>(bytes) / 1073741824.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(static_cast<double>(bytes) / 1048576.0,
                                       0, 'f', 0);
}

std::string llmProviderModelKey(const QString &providerId)
{
    return "/settings/llm/providerModels/" + providerId.toStdString();
}

nlohmann::json findLlmProviderJson(const QString &id)
{
    const nlohmann::json providers = llmProvidersJson();
    if (providers.is_array()) {
        for (const auto &provider : providers) {
            if (provider.is_object() &&
                provider.value("id", std::string()) == id.toStdString())
            {
                return provider;
            }
        }
    }
    return nlohmann::json::object();
}

QString modelJsonString(const nlohmann::json &model, const std::string &key,
                        const QString &fallback = {})
{
    return model.contains(key) && model[key].is_string()
               ? QString::fromStdString(model[key].get<std::string>())
               : fallback;
}

qint64 modelJsonInt64(const nlohmann::json &model, const std::string &key,
                      qint64 fallback = 0)
{
    return model.contains(key) && model[key].is_number_integer()
               ? static_cast<qint64>(model[key].get<std::int64_t>())
               : fallback;
}

bool modelJsonBool(const nlohmann::json &model, const std::string &key,
                   bool fallback = false)
{
    return model.contains(key) && model[key].is_boolean()
               ? model[key].get<bool>()
               : fallback;
}

QUrl modelArchiveUrl(const nlohmann::json &model)
{
    return QUrl(modelJsonString(model, "url"));
}

QString asrPresetPointer(std::size_t index)
{
    return QStringLiteral("/asrPresets/%1").arg(index);
}

nlohmann::json modelJsonAtPointer(const QString &pointer)
{
    if (pointer.isEmpty()) {
        return nlohmann::json::object();
    }
    return talkinput::appConfigValue(pointer.toStdString());
}

QString currentLlmSystemPrompt()
{
    return talkinput::appConfigString("/settings/llm/systemPrompt");
}

QString currentLlmUserPrompt()
{
    return talkinput::appConfigString("/settings/llm/userPrompt");
}

QString languageDisplay(const QString &lang)
{
    if (lang == QStringLiteral("multilingual")) {
        return QCoreApplication::translate(
            "talkinput::AsrSettingWidget",
            "\345\244\232\350\257\255\350\250\200");
    }
    if (lang == QStringLiteral("zh") || lang == QStringLiteral("zh-CN")) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "\344\270\255\346\226\207");
    }
    if (lang == QStringLiteral("en") || lang == QStringLiteral("en-US")) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "English");
    }
    if (lang == QStringLiteral("zh,en") || lang == QStringLiteral("zh,en-US")) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "\344\270\255\350\213\261");
    }
    if (lang == QStringLiteral("system")) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "System");
    }
    return lang;
}

QString streamingLabel(bool streaming)
{
    if (streaming) {
        return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                           "\345\256\236\346\227\266");
    }
    return QCoreApplication::translate("talkinput::AsrSettingWidget",
                                       "\351\235\236\345\256\236\346\227\266");
}

bool systemSpeechRecognizerSupported()
{
#if defined(Q_OS_WIN)
    return true;
#else
    return false;
#endif
}

bool shouldShowAsrPreset(const nlohmann::json &preset)
{
    return modelJsonString(preset, "type") != QStringLiteral("System") ||
           systemSpeechRecognizerSupported();
}

} // namespace

namespace talkinput
{

AsrSettingWidget::AsrSettingWidget(QWidget *parent)
    : QWidget(parent), m_ui(std::make_unique<Ui::AsrSettingWidget>()),
      m_networkManager(new QNetworkAccessManager(this))
{
    SPDLOG_DEBUG("AsrSettingWidget: constructor begin");

    connect(m_networkManager, &QNetworkAccessManager::finished, this,
            &AsrSettingWidget::onDownloadFinished);

    m_ui->setupUi(this);

    const nlohmann::json llmProviders = llmProvidersJson();
    auto *providerCombo = m_ui->providerCombo;
    for (const auto &provider : llmProviders) {
        if (!provider.is_object()) {
            continue;
        }
        providerCombo->addItem(qs(provider.value("name", std::string())),
                               qs(provider.value("id", std::string())));
    }

    auto *endpointEdit = m_ui->endpointEdit;
    auto *modelCombo = m_ui->llmModelCombo;
    modelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));
    auto *apiKeyEdit = m_ui->apiKeyEdit;
    auto *promptEditBtn = m_ui->promptEditButton;
    setButtonIcon(promptEditBtn, ":/resources/icons/edit.svg", 18);
    refreshPromptLabel();

    auto providerAt = [providerCombo](int index) -> nlohmann::json {
        if (index >= 0 && index < providerCombo->count()) {
            const QString providerId =
                providerCombo->itemData(index).toString();
            nlohmann::json provider = findLlmProviderJson(providerId);
            if (provider.is_object() && !provider.empty()) {
                return provider;
            }
        }
        return firstLlmProviderJson();
    };
    auto applyProvider = [providerCombo, endpointEdit, modelCombo](
                             const nlohmann::json &provider, bool persist) {
        const bool custom = provider.value("custom", false);
        const QString providerId = qs(provider.value("id", std::string()));
        QString endpoint;
        QString model;
        if (custom) {
            endpoint =
                appConfigString("/settings/llm/customEndpoint").trimmed();
            if (endpoint.isEmpty()) {
                endpoint = appConfigString("/settings/llm/endpoint").trimmed();
            }
            model = appConfigString("/settings/llm/customModel").trimmed();
            if (model.isEmpty()) {
                model = appConfigString("/settings/llm/model").trimmed();
            }
        }
        else {
            endpoint = qs(provider.value("endpoint", std::string())).trimmed();
            model = appConfigString(llmProviderModelKey(providerId)).trimmed();
            if (model.isEmpty()) {
                model = qs(provider.value("model", std::string())).trimmed();
            }
        }

        {
            const QSignalBlocker endpointBlocker(endpointEdit);
            const QSignalBlocker modelBlocker(modelCombo);
            endpointEdit->setText(endpoint);
            modelCombo->clear();
            const nlohmann::json models =
                provider.value("models", nlohmann::json::array());
            for (const auto &presetModel : models) {
                if (presetModel.is_string()) {
                    modelCombo->addItem(qs(presetModel.get<std::string>()));
                }
            }
            if (!model.isEmpty() && modelCombo->findText(model) < 0) {
                modelCombo->addItem(model);
            }
            modelCombo->setEditText(model);
        }
        endpointEdit->setReadOnly(!custom);

        if (!persist) {
            return;
        }

        setAppConfigValue("/settings/llm/providerId",
                          provider.value("id", std::string()));
        setAppConfigValue("/settings/llm/endpoint", endpoint);
        setAppConfigValue("/settings/llm/model", model);
        setAppConfigValue(llmProviderModelKey(providerId), model);
        if (custom) {
            setAppConfigValue("/settings/llm/customEndpoint", endpoint);
            setAppConfigValue("/settings/llm/customModel", model);
        }
        int idx = providerCombo->findData(providerId);
        if (idx >= 0 && idx != providerCombo->currentIndex()) {
            providerCombo->setCurrentIndex(idx);
        }
    };

    {
        QString providerId = appConfigString("/settings/llm/providerId");
        int providerIndex = providerCombo->findData(providerId);
        if (providerIndex < 0) {
            providerIndex = 0;
        }
        if (providerIndex >= 0) {
            providerCombo->setCurrentIndex(providerIndex);
            applyProvider(providerAt(providerIndex), false);
        }
        apiKeyEdit->setText(appConfigString("/settings/llm/apiKey"));
    }

    connect(providerCombo, &QComboBox::currentIndexChanged, this,
            [this, providerCombo, providerAt, applyProvider](int index) {
                const auto provider = providerAt(index);
                applyProvider(provider, true);
                spdlog::get("statusbar")
                    ->info("{}", tr("LLM provider saved: %1")
                                     .arg(providerCombo->itemText(
                                         providerCombo->currentIndex())));
            });
    connect(endpointEdit, &QLineEdit::editingFinished, this,
            [this, providerCombo, endpointEdit]() {
                const QString endpoint = endpointEdit->text().trimmed();
                setAppConfigValue("/settings/llm/endpoint", endpoint);
                if (providerCombo->currentData().toString() == "custom") {
                    setAppConfigValue("/settings/llm/customEndpoint", endpoint);
                }
                spdlog::get("statusbar")->info("{}", tr("LLM endpoint saved"));
            });
    auto saveModel = [this, providerCombo, modelCombo]() {
        const QString model = modelCombo->currentText().trimmed();
        const QString providerId = providerCombo->currentData().toString();
        setAppConfigValue("/settings/llm/model", model);
        setAppConfigValue(llmProviderModelKey(providerId), model);
        if (providerId == "custom") {
            setAppConfigValue("/settings/llm/customModel", model);
        }
        spdlog::get("statusbar")->info("{}", tr("LLM model saved"));
    };
    connect(modelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(modelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });
    connect(modelCombo, &QComboBox::currentTextChanged, this,
            [providerCombo, modelCombo]() {
                const QString model = modelCombo->currentText().trimmed();
                if (model.isEmpty()) {
                    return;
                }
                const QString providerId =
                    providerCombo->currentData().toString();
                setAppConfigValue("/settings/llm/model", model);
                setAppConfigValue(llmProviderModelKey(providerId), model);
                if (providerId == "custom") {
                    setAppConfigValue("/settings/llm/customModel", model);
                }
            });
    connect(apiKeyEdit, &QLineEdit::editingFinished, this,
            [this, apiKeyEdit]() {
                setAppConfigValue("/settings/llm/apiKey",
                                  apiKeyEdit->text().trimmed());
                spdlog::get("statusbar")->info("{}", tr("LLM API key saved"));
            });
    connect(promptEditBtn, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("LLM User Prompt"));
        dialog.resize(580, 360);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(10);

        const QString hint = tr("Available variables: {{input}}, {{context}}, "
                                "{{hotwords}}");

        auto *usrLabel = new QLabel(QString("<b>%1</b><br><small>%2</small>")
                                        .arg(tr("User Prompt"), hint),
                                    &dialog);
        usrLabel->setWordWrap(true);
        layout->addWidget(usrLabel);

        auto *usrEditor = new QTextEdit(&dialog);
        usrEditor->setAcceptRichText(false);
        usrEditor->setPlaceholderText(
            tr("Use {{input}}, {{context}}, and {{hotwords}} as needed"));
        usrEditor->setPlainText(currentLlmUserPrompt());
        layout->addWidget(usrEditor, 1);

        auto *buttons = new QDialogButtonBox(&dialog);
        buttons->addButton(QDialogButtonBox::Save);
        buttons->addButton(QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, &dialog,
                &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog,
                &QDialog::reject);
        layout->addWidget(buttons);

        if (dialog.exec() != QDialog::Accepted) {
            return;
        }

        setAppConfigValue("/settings/llm/userPrompt",
                          usrEditor->toPlainText().trimmed());
        refreshPromptLabel();
        spdlog::get("statusbar")->info("{}", tr("LLM prompts saved"));
    });

    auto *hotwordsBtn = m_ui->hotwordsButton;
    connect(hotwordsBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);

    auto *llmPostProcessCheck = m_ui->llmPostProcessCheck;
    llmPostProcessCheck->setChecked(
        appConfigBool("/settings/llm/llmPostProcessEnableForAsr", false));
    connect(
        llmPostProcessCheck, &QCheckBox::toggled, this, [this](bool checked) {
            setAppConfigValue("/settings/llm/llmPostProcessEnableForAsr", checked);
            spdlog::get("statusbar")
                ->info("{}", checked ? tr("LLM post-processing enabled.")
                                     : tr("LLM post-processing disabled."));
        });

    auto *ocrContextCheck = m_ui->ocrContextCheck;
    ocrContextCheck->setChecked(
        appConfigBool("/settings/ocr/ocrContextEnableForAsr", false));
    connect(ocrContextCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setAppConfigValue("/settings/ocr/ocrContextEnableForAsr", checked);
        spdlog::get("statusbar")
            ->info("{}", checked ? tr("OCR context enabled.")
                                 : tr("OCR context disabled."));
    });

    // ── Load model presets from config ───────────────────────────
    SPDLOG_DEBUG("AsrSettingWidget: loading model presets");
    const nlohmann::json asrPresets = talkinput::appConfigValue("/asrPresets");

    // Populate ComboBox with ASR models. Each item stores the preset's
    // JSON Pointer so later actions can read the live appConfig directly.
    // Format: "Name - 实时/非实时 - 语言"
    m_ui->modelCombo->clear();
    if (asrPresets.is_array()) {
        for (std::size_t i = 0; i < asrPresets.size(); ++i) {
            const nlohmann::json &preset = asrPresets[i];
            if (!preset.is_object()) {
                continue;
            }
            if (!shouldShowAsrPreset(preset)) {
                continue;
            }
            const QString name = modelJsonString(preset, "name");
            const QString dirName = modelJsonString(preset, "modelDirName");
            SPDLOG_DEBUG("AsrSettingWidget: preset {} ({})", name, dirName);

            const QString label =
                QStringLiteral("%1 - %2 - %3")
                    .arg(name,
                         streamingLabel(
                             modelJsonBool(preset, "streamingSupport")),
                         languageDisplay(modelJsonString(preset, "languages")));
            m_ui->modelCombo->addItem(label, asrPresetPointer(i));
        }
    }

    // Restore saved model selection by providerId
    const QString savedAsrId =
        talkinput::appConfigString("/settings/asr/providerId");
    int restoreIndex = -1;
    if (!savedAsrId.isEmpty()) {
        for (int ci = 0; ci < m_ui->modelCombo->count(); ++ci) {
            const nlohmann::json model =
                modelJsonAtPointer(m_ui->modelCombo->itemData(ci).toString());
            if (modelJsonString(model, "id") == savedAsrId) {
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

    // Download/delete buttons
    connect(m_ui->downloadButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onDownloadCurrent);
    connect(m_ui->deleteButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onDeleteCurrent);
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseCurrent);

    // Initial status update
    if (m_ui->modelCombo->count() > 0) {
        onModelChanged(m_ui->modelCombo->currentIndex());
    }

    // Apply icons to bottom buttons
    setButtonIcon(hotwordsBtn, ":/resources/icons/hotwords.svg", 22);
    hotwordsBtn->setProperty("buttonRole", "icon");
    SPDLOG_DEBUG("AsrSettingWidget: constructor end");
}

AsrSettingWidget::~AsrSettingWidget() = default;

void AsrSettingWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_ui->retranslateUi(this);
        if (m_ui->llmModelCombo->lineEdit()) {
            m_ui->llmModelCombo->lineEdit()->setPlaceholderText(
                tr("Model name sent to the LLM service"));
        }
        refreshPromptLabel();
        refreshStatus();
    }
}

void AsrSettingWidget::refreshPromptLabel()
{
    const QString usrPrompt = currentLlmUserPrompt().simplified();
    m_ui->promptLabel->setText(
        QString("[User] %1 \342\200\246").arg(usrPrompt.left(40)));
    m_ui->promptLabel->setToolTip(QString("User: %1").arg(usrPrompt));
}

// ── Model selection ──────────────────────────────────────────────

void AsrSettingWidget::onModelChanged(int index)
{
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        return;
    }

    const nlohmann::json model =
        modelJsonAtPointer(m_ui->modelCombo->itemData(index).toString());
    if (!model.is_object()) {
        return;
    }

    const bool installed = isInstalled(model);
    const QString modelName = modelJsonString(model, "name");
    const bool systemModel =
        modelJsonString(model, "type") == QStringLiteral("System");
    const qint64 modelSize = modelJsonInt64(model, "size");

    m_ui->downloadButton->setEnabled(false);
    m_ui->deleteButton->setEnabled(false);
    m_ui->useButton->setEnabled(false);

    if (systemModel) {
        m_ui->statusLabel->setText(
            tr("Uses the operating system speech recognizer."));
        m_ui->useButton->setEnabled(true);
    }
    else if (installed) {
        const QString sizeStr =
            modelSize > 0 ? QString(" (%1)").arg(formatSize(modelSize))
                          : QString();
        m_ui->statusLabel->setText(
            tr("Installed: %1%2 \342\200\224 click \"Use\" to load")
                .arg(modelName, sizeStr));
        m_ui->deleteButton->setEnabled(true);
        m_ui->useButton->setEnabled(true);
    }
    else {
        const QString sizeStr =
            modelSize > 0 ? formatSize(modelSize) : QString();
        m_ui->statusLabel->setText(tr("Not installed (%1, %2). Click Download.")
                                       .arg(modelName, sizeStr));
        m_ui->downloadButton->setEnabled(true);
    }
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
    if (ci < 0) {
        return {};
    }
    return m_ui->modelCombo->currentData().toString();
}

// ── Punctuation model auto-load ──────────────────────────────────

// ── Actions ──────────────────────────────────────────────────────

void AsrSettingWidget::onDownloadCurrent()
{
    const QString modelPointer = currentModelPointer();
    if (modelPointer.isEmpty() || m_activeDownloadReply ||
        !m_downloadQueue.isEmpty())
    {
        return;
    }

    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    const QUrl archiveUrl = modelArchiveUrl(m);
    if (archiveUrl.isEmpty()) {
        return;
    }

    QDir cache(cacheDir());
    if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) {
        return;
    }

    m_downloadQueue.enqueue(modelPointer);

    // Queue punctuation model partner if configured and not installed
    const nlohmann::json punct =
        m.value("postPunctuationModel", nlohmann::json::object());
    if (punct.is_object() && !punct.empty() && !isInstalled(punct) &&
        !modelArchiveUrl(punct).isEmpty())
    {
        m_downloadQueue.enqueue(modelPointer +
                                QStringLiteral("/postPunctuationModel"));
    }

    startModelDownload(m_downloadQueue.dequeue());
}

void AsrSettingWidget::startModelDownload(const QString &modelPointer)
{
    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    if (!m.is_object()) {
        return;
    }
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
        m_downloadQueue.clear();
        return;
    }

    spdlog::get("statusbar")
        ->info("{}", tr("Downloading %1...").arg(modelName));
    m_ui->downloadButton->setEnabled(false);

    QNetworkRequest req(archiveUrl);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    m_activeDownloadReply = m_networkManager->get(req);
    connect(m_activeDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_activeDownloadReply && m_activeDownloadFile) {
            m_activeDownloadFile->write(m_activeDownloadReply->readAll());
        }
    });
    connect(m_activeDownloadReply, &QNetworkReply::downloadProgress, this,
            [this, modelName](qint64 received, qint64 total) {
                if (total <= 0) {
                    return;
                }
                const int pct = static_cast<int>(received * 100 / total);
                spdlog::get("statusbar")
                    ->info("{}",
                           tr("Downloading %1... %2%").arg(modelName).arg(pct));
            });
}

void AsrSettingWidget::onDeleteCurrent()
{
    const nlohmann::json m = modelJsonAtPointer(currentModelPointer());
    if (!m.is_object()) {
        return;
    }

    const QString modelName = modelJsonString(m, "name");
    const QString dir =
        QDir(cacheDir()).filePath(modelJsonString(m, "modelDirName"));
    if (!QFileInfo(dir).isDir()) {
        spdlog::get("statusbar")->info("{}", tr("Model not found"));
        return;
    }

    auto result = QMessageBox::question(
        this, tr("Delete Model"),
        tr("Delete %1?\n\n%2").arg(modelName, QDir::toNativeSeparators(dir)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (result != QMessageBox::Yes) {
        return;
    }

    QDir(dir).removeRecursively();
    SPDLOG_INFO("Deleted model: {} ({})", modelName, dir);
    spdlog::get("statusbar")->info("{}", tr("Deleted: %1").arg(modelName));
    refreshStatus();
}

void AsrSettingWidget::onUseCurrent()
{
    const QString modelPointer = currentModelPointer();
    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    if (!m.is_object()) {
        return;
    }

    if (!isInstalled(m)) {
        spdlog::get("statusbar")
            ->info("{}", tr("Model not installed. Download first."));
        return;
    }

    activateModel(modelPointer);

    spdlog::get("statusbar")
        ->info("{}", tr("Model loaded: %1").arg(modelJsonString(m, "name")));
}

void AsrSettingWidget::activateModel(const QString &modelPointer)
{
    const nlohmann::json m = modelJsonAtPointer(modelPointer);
    if (!m.is_object()) {
        return;
    }

    const QString modelName = modelJsonString(m, "name");
    const QString modelType = modelJsonString(m, "type");
    const bool systemModel = modelType == QStringLiteral("System");
    const QString dir =
        systemModel
            ? QString()
            : QDir(cacheDir()).filePath(modelJsonString(m, "modelDirName"));

    if (!systemModel && !QFileInfo(dir).isDir()) {
        return;
    }

    if (m_downloadTargetPointer == modelPointer) {
        m_downloadTargetPointer.clear();
    }

    SPDLOG_INFO("Recognition model set: {} ({})", modelName, dir);
    setAppConfigValue("/settings/asr/providerId",
                      modelJsonString(m, "id"));
    emit modelSelected(dir, modelName, modelType);
    spdlog::get("statusbar")
        ->info("{}", tr("Model selected: %1").arg(modelName));
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

    // Read hotwords as array
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

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    // Save hotwords as array
    {
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
    }
    spdlog::get("statusbar")->info("{}", tr("Hot words saved"));
    emit hotwordsChanged();
}

// ── Download finished ────────────────────────────────────────────

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

    if (reply) {
        reply->deleteLater();
    }

    if (failed) {
        QFile::remove(m_activeDownloadTempPath);
        m_activeDownloadFile.reset();
        m_downloadQueue.clear();
        spdlog::get("statusbar")->info("{}", tr("Download failed"));
        refreshStatus();
        return;
    }

    QFile::remove(m_activeDownloadPath);
    QFile::rename(m_activeDownloadTempPath, m_activeDownloadPath);
    m_activeDownloadFile.reset();

    spdlog::get("statusbar")->info("{}", tr("Extracting..."));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString err;
    if (!extractArchive(m_activeDownloadPath, cacheDir(), &err)) {
        m_downloadQueue.clear();
        spdlog::get("statusbar")
            ->info("{}", tr("Extraction failed: %1").arg(err));
        refreshStatus();
        return;
    }

    if (!modelPointer.isEmpty()) {
        const nlohmann::json m = modelJsonAtPointer(modelPointer);
        const QString modelName = modelJsonString(m, "name");
        const QString modelDir =
            QDir(cacheDir()).filePath(modelJsonString(m, "modelDirName"));
        if (QFileInfo(modelDir).isDir() || isInstalled(m)) {
            if (modelPointer.endsWith(QStringLiteral("/postPunctuationModel")))
            {
                spdlog::get("statusbar")
                    ->info("{}",
                           tr("Punctuation model ready: %1").arg(modelName));
            }
            else {
                // Don't auto-load — user must click "Use"
                spdlog::get("statusbar")
                    ->info("{}", tr("Downloaded: %1. Click \"Use\" to load.")
                                     .arg(modelName));
            }
        }
    }

    if (!m_downloadQueue.isEmpty()) {
        startModelDownload(m_downloadQueue.dequeue());
    }
    else {
        refreshStatus();
    }
}

// ── Helpers ──────────────────────────────────────────────────────

bool AsrSettingWidget::isInstalled(const nlohmann::json &m) const
{
    if (!m.is_object()) {
        return false;
    }
    const QString modelDirName = modelJsonString(m, "modelDirName");
    if (modelJsonString(m, "type") == QStringLiteral("System") ||
        modelDirName.isEmpty())
    {
        return true;
    }
    const QString path = QDir(cacheDir()).filePath(modelDirName);
    SPDLOG_DEBUG("model {} cache path: {}", modelDirName, path);
    return QFileInfo(path).isDir();
}

} // namespace talkinput
