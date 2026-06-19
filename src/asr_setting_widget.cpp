#include "asr_setting_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "utils.h"

#include <QCheckBox>
#include <QComboBox>
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
    return talkinput::appConfigValue("/llmPostProcessing/providers");
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

} // namespace

namespace talkinput
{

AsrSettingWidget::AsrSettingWidget(QWidget *parent)
    : QWidget(parent), m_networkManager(new QNetworkAccessManager(this))
{
    SPDLOG_DEBUG("AsrSettingWidget: constructor begin");

    connect(m_networkManager, &QNetworkAccessManager::finished, this,
            &AsrSettingWidget::onDownloadFinished);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Model selector ───────────────────────────────────────────
    auto *modelGroup = new QGroupBox(tr("Recognition Model"), this);
    auto *modelLayout = new QVBoxLayout(modelGroup);
    modelLayout->setContentsMargins(10, 10, 10, 10);
    modelLayout->setSpacing(8);

    auto *modelRow = new QHBoxLayout();
    modelRow->setSpacing(8);
    m_modelCombo = new QComboBox(modelGroup);
    modelRow->addWidget(new QLabel(tr("Model:"), modelGroup));
    modelRow->addWidget(m_modelCombo, 1);
    modelLayout->addLayout(modelRow);

    m_statusLabel = new QLabel(modelGroup);
    m_statusLabel->setWordWrap(true);
    modelLayout->addWidget(m_statusLabel);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    m_dlBtn = new QPushButton(tr("Download"), modelGroup);
    m_delBtn = new QPushButton(tr("Delete"), modelGroup);
    m_useBtn = new QPushButton(tr("Use"), modelGroup);
    m_dlBtn->setEnabled(false);
    m_delBtn->setEnabled(false);
    m_useBtn->setEnabled(false);
    m_useBtn->setStyleSheet("font-weight: bold;");
    btnRow->addWidget(m_dlBtn);
    btnRow->addWidget(m_delBtn);
    btnRow->addWidget(m_useBtn);
    btnRow->addStretch();
    modelLayout->addLayout(btnRow);

    root->addWidget(modelGroup);

    // ── LLM Service ──────────────────────────────────────────────
    auto *llmGroup = new QGroupBox(tr("LLM Service"), this);
    auto *llmForm = new QFormLayout(llmGroup);
    llmForm->setContentsMargins(10, 10, 10, 10);
    llmForm->setSpacing(8);

    const nlohmann::json llmProviders = llmProvidersJson();
    auto *providerCombo = new QComboBox(llmGroup);
    for (const auto &provider : llmProviders) {
        if (!provider.is_object()) {
            continue;
        }
        providerCombo->addItem(qs(provider.value("name", std::string())),
                               qs(provider.value("id", std::string())));
    }

    auto *endpointEdit = new QLineEdit(llmGroup);
    endpointEdit->setPlaceholderText(
        tr("OpenAI-compatible chat completions endpoint"));
    auto *modelCombo = new QComboBox(llmGroup);
    modelCombo->setEditable(true);
    modelCombo->setInsertPolicy(QComboBox::NoInsert);
    modelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));
    auto *apiKeyEdit = new QLineEdit(llmGroup);
    apiKeyEdit->setEchoMode(QLineEdit::Password);
    apiKeyEdit->setPlaceholderText(tr("Optional API key"));
    auto *promptWidget = new QWidget(llmGroup);
    auto *promptLayout = new QHBoxLayout(promptWidget);
    promptLayout->setContentsMargins(0, 0, 0, 0);
    promptLayout->setSpacing(6);
    auto *promptLabel = new QLabel(promptWidget);
    promptLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    promptLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *promptEditBtn = new QPushButton(promptWidget);
    promptEditBtn->setToolTip(tr("Edit LLM prompt"));
    setButtonIcon(promptEditBtn, ":/resources/icons/edit.svg", 18);
    promptLayout->addWidget(promptLabel, 1);
    promptLayout->addWidget(promptEditBtn);

    auto refreshPromptLabel = [promptLabel]() {
        const QString sysPrompt = currentLlmSystemPrompt().simplified();
        const QString usrPrompt = currentLlmUserPrompt().simplified();
        promptLabel->setText(
            QString("[System] %1 \342\200\246 [User] %2 \342\200\246")
                .arg(sysPrompt.left(40), usrPrompt.left(40)));
        promptLabel->setToolTip(
            QString("System: %1\nUser: %2").arg(sysPrompt, usrPrompt));
    };
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
                spdlog::get("statusbar")->info("{}", tr("LLM endpoint saved."));
            });
    auto saveModel = [this, providerCombo, modelCombo]() {
        const QString model = modelCombo->currentText().trimmed();
        const QString providerId = providerCombo->currentData().toString();
        setAppConfigValue("/settings/llm/model", model);
        setAppConfigValue(llmProviderModelKey(providerId), model);
        if (providerId == "custom") {
            setAppConfigValue("/settings/llm/customModel", model);
        }
        spdlog::get("statusbar")->info("{}", tr("LLM model saved."));
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
                spdlog::get("statusbar")->info("{}", tr("LLM API key saved."));
            });
    connect(promptEditBtn, &QPushButton::clicked, this,
            [this, refreshPromptLabel]() {
                QDialog dialog(this);
                dialog.setWindowTitle(tr("LLM Prompts"));
                dialog.resize(580, 520);

                auto *layout = new QVBoxLayout(&dialog);
                layout->setContentsMargins(16, 16, 16, 16);
                layout->setSpacing(10);

                const QString hint =
                    tr("Available variables: {{input}}, {{context}}, "
                       "{{hotwords}}");

                // -- System Prompt --
                auto *sysLabel =
                    new QLabel(QString("<b>%1</b><br><small>%2</small>")
                                   .arg(tr("System Prompt"), hint),
                               &dialog);
                sysLabel->setWordWrap(true);
                layout->addWidget(sysLabel);

                auto *sysEditor = new QTextEdit(&dialog);
                sysEditor->setAcceptRichText(false);
                sysEditor->setPlaceholderText(tr(
                    "\344\276\213\345\246\202\357\274\232\344\275\240\346\230"
                    "\257\344\270\200\344\270\252\346\234\211\345\270\256\345"
                    "\212\251\347\232\204\345\212\251\346\211\213"));
                sysEditor->setPlainText(currentLlmSystemPrompt());
                sysEditor->setMaximumHeight(150);
                layout->addWidget(sysEditor);

                // -- User Prompt --
                auto *usrLabel =
                    new QLabel(QString("<b>%1</b><br><small>%2</small>")
                                   .arg(tr("User Prompt"), hint),
                               &dialog);
                usrLabel->setWordWrap(true);
                layout->addWidget(usrLabel);

                auto *usrEditor = new QTextEdit(&dialog);
                usrEditor->setAcceptRichText(false);
                usrEditor->setPlaceholderText(tr(
                    "Use {{input}}, {{context}}, and {{hotwords}} as needed"));
                usrEditor->setPlainText(currentLlmUserPrompt());
                usrEditor->setMaximumHeight(150);
                layout->addWidget(usrEditor);

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

                setAppConfigValue("/settings/llm/systemPrompt",
                                  sysEditor->toPlainText().trimmed());
                setAppConfigValue("/settings/llm/userPrompt",
                                  usrEditor->toPlainText().trimmed());
                refreshPromptLabel();
                spdlog::get("statusbar")->info("{}", tr("LLM prompts saved."));
            });

    llmForm->addRow(tr("Provider"), providerCombo);
    llmForm->addRow(tr("Endpoint"), endpointEdit);
    llmForm->addRow(tr("Model"), modelCombo);
    llmForm->addRow(tr("API Key"), apiKeyEdit);
    llmForm->addRow(tr("Prompt"), promptWidget);
    root->addWidget(llmGroup);

    // ── Bottom row ───────────────────────────────────────────────
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
    llmPostProcessCheck->setChecked(
        appConfigBool("/settings/llm/postProcessingEnabled", false));
    connect(
        llmPostProcessCheck, &QCheckBox::toggled, this, [this](bool checked) {
            setAppConfigValue("/settings/llm/postProcessingEnabled", checked);
            spdlog::get("statusbar")
                ->info("{}", checked ? tr("LLM post-processing enabled.")
                                     : tr("LLM post-processing disabled."));
        });

    auto *ocrContextCheck = new QCheckBox(tr("OCR focused context"), this);
    ocrContextCheck->setToolTip(
        tr("Use OCR text around the focused input as LLM context"));
    ocrContextCheck->setChecked(
        appConfigBool("/settings/ocr/useFocusedInputContext", false));
    connect(ocrContextCheck, &QCheckBox::toggled, this, [this](bool checked) {
        setAppConfigValue("/settings/ocr/useFocusedInputContext", checked);
        spdlog::get("statusbar")
            ->info("{}", checked ? tr("OCR context enabled.")
                                 : tr("OCR context disabled."));
    });

    bottomRow->addWidget(archiveBtn);
    bottomRow->addWidget(openBtn);
    bottomRow->addWidget(hotwordsBtn);
    bottomRow->addWidget(llmPostProcessCheck);
    bottomRow->addWidget(ocrContextCheck);
    bottomRow->addStretch();
    root->addLayout(bottomRow);

    // ── Load model presets from config ───────────────────────────
    SPDLOG_DEBUG("AsrSettingWidget: loading model presets");
    const nlohmann::json asrPresets = talkinput::appConfigValue("/asrPresets");

    // Populate ComboBox with ASR models. Each item stores the preset's
    // JSON Pointer so later actions can read the live appConfig directly.
    // Format: "Name - 实时/非实时 - 语言"
    m_modelCombo->clear();
    if (asrPresets.is_array()) {
        for (std::size_t i = 0; i < asrPresets.size(); ++i) {
            const nlohmann::json &preset = asrPresets[i];
            if (!preset.is_object()) {
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
            m_modelCombo->addItem(label, asrPresetPointer(i));
        }
    }

    // Restore saved model selection (by modelDirName)
    const QString savedDirName =
        QFileInfo(appConfigString("/settings/model/directory")).fileName();
    const QString savedType = appConfigString("/settings/model/type");
    int restoreIndex = -1;
    if (savedType == QStringLiteral("System")) {
        for (int ci = 0; ci < m_modelCombo->count(); ++ci) {
            const nlohmann::json model =
                modelJsonAtPointer(m_modelCombo->itemData(ci).toString());
            if (modelJsonString(model, "type") == savedType) {
                restoreIndex = ci;
                break;
            }
        }
    }
    else if (!savedDirName.isEmpty()) {
        for (int ci = 0; ci < m_modelCombo->count(); ++ci) {
            const nlohmann::json model =
                modelJsonAtPointer(m_modelCombo->itemData(ci).toString());
            if (modelJsonString(model, "modelDirName") == savedDirName) {
                restoreIndex = ci;
                break;
            }
        }
    }
    if (restoreIndex >= 0) {
        m_modelCombo->setCurrentIndex(restoreIndex);
    }

    connect(m_modelCombo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onModelChanged);

    // Download/delete buttons
    connect(m_dlBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onDownloadCurrent);
    connect(m_delBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onDeleteCurrent);
    connect(m_useBtn, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseCurrent);

    // Initial status update
    if (m_modelCombo->count() > 0) {
        onModelChanged(m_modelCombo->currentIndex());
    }

    // Apply icons to bottom buttons
    setButtonIcon(archiveBtn, ":/resources/icons/folder-plus.svg", 22);
    setButtonIcon(openBtn, ":/resources/icons/folder.svg", 22);
    setButtonIcon(hotwordsBtn, ":/resources/icons/hotwords.svg", 22);
    archiveBtn->setProperty("buttonRole", "icon");
    openBtn->setProperty("buttonRole", "icon");
    hotwordsBtn->setProperty("buttonRole", "icon");
    SPDLOG_DEBUG("AsrSettingWidget: constructor end");
}

AsrSettingWidget::~AsrSettingWidget() = default;

// ── Model selection ──────────────────────────────────────────────

void AsrSettingWidget::onModelChanged(int index)
{
    if (index < 0 || index >= m_modelCombo->count()) {
        return;
    }

    const nlohmann::json model =
        modelJsonAtPointer(m_modelCombo->itemData(index).toString());
    if (!model.is_object()) {
        return;
    }

    const bool installed = isInstalled(model);
    const QString modelName = modelJsonString(model, "name");
    const bool systemModel =
        modelJsonString(model, "type") == QStringLiteral("System");
    const qint64 modelSize = modelJsonInt64(model, "size");

    m_dlBtn->setEnabled(false);
    m_delBtn->setEnabled(false);
    m_useBtn->setEnabled(false);

    if (systemModel) {
        m_statusLabel->setText(
            tr("Uses the operating system speech recognizer."));
        m_useBtn->setEnabled(true);
    }
    else if (installed) {
        const QString sizeStr =
            modelSize > 0 ? QString(" (%1)").arg(formatSize(modelSize))
                          : QString();
        m_statusLabel->setText(
            tr("Installed: %1%2 \342\200\224 click \"Use\" to load")
                .arg(modelName, sizeStr));
        m_delBtn->setEnabled(true);
        m_useBtn->setEnabled(true);
    }
    else {
        const QString sizeStr =
            modelSize > 0 ? formatSize(modelSize) : QString();
        m_statusLabel->setText(tr("Not installed (%1, %2). Click Download.")
                                   .arg(modelName, sizeStr));
        m_dlBtn->setEnabled(true);
    }
}

void AsrSettingWidget::refreshStatus()
{
    if (m_modelCombo->count() > 0) {
        onModelChanged(m_modelCombo->currentIndex());
    }
}

QString AsrSettingWidget::currentModelPointer() const
{
    const int ci = m_modelCombo->currentIndex();
    if (ci < 0) {
        return {};
    }
    return m_modelCombo->currentData().toString();
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
    m_dlBtn->setEnabled(false);

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
        spdlog::get("statusbar")->info("{}", tr("Model not found."));
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
    setAppConfigValue("/settings/model/directory", dir);
    setAppConfigValue("/settings/model/name", modelName);
    setAppConfigValue("/settings/model/type", modelType);
    emit modelSelected(dir, modelName, modelType);
    spdlog::get("statusbar")
        ->info("{}", tr("Model selected: %1").arg(modelName));
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

    spdlog::get("statusbar")->info("{}", tr("Extracting..."));
    QCoreApplication::processEvents();

    QString err;
    if (!extractArchive(path, dest.absolutePath(), &err)) {
        QMessageBox::warning(this, tr("Extraction failed"),
                             tr("Failed:\n%1").arg(err));
        spdlog::get("statusbar")->info("{}", tr("Extraction failed."));
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
        SPDLOG_INFO("Extracted model: {}", modelDir);
        emit modelSelected(modelDir, base, {});
        spdlog::get("statusbar")
            ->info("{}",
                   tr("Extracted: %1").arg(QDir::toNativeSeparators(modelDir)));
    }
    else {
        spdlog::get("statusbar")
            ->info("{}", tr("Directory not found: %1")
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
    editor->setPlainText(appConfigString("/settings/model/hotwords"));
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    setAppConfigValue("/settings/model/hotwords",
                      editor->toPlainText().trimmed());
    spdlog::get("statusbar")->info("{}", tr("Hot words saved."));
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
        spdlog::get("statusbar")->info("{}", tr("Download failed."));
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
