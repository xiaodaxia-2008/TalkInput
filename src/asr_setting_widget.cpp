#include "asr_setting_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "asr_config.h"
#include "llm_config.h"
#include "logging.h"
#include "ocr_config.h"
#include "ui_asr_setting_widget.h"
#include "utils.h"
#include "voice_input_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
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
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace talkinput
{

namespace
{

// ── Shared helpers ────────────────────────────────────────────────────

QString llmProviderId(const QComboBox *combo)
{
    return combo->currentData().toString();
}

QString asrPresetPointer(const QString &providerId)
{
    return QStringLiteral("/asrPresets/%1").arg(providerId);
}

nlohmann::json llmProviderPreset(const QComboBox *combo)
{
    return talkinput::llmProviderPreset(llmProviderId(combo));
}

void saveLlmSetting(const QComboBox *combo, const QString &key,
                    const QString &value)
{
    setLlmProviderSetting(llmProviderId(combo), key, value);
}

QString asrModelLabel(const nlohmann::json &m)
{
    auto langLabel = [](const QString &c) -> QString {
        if (c == QLatin1StringView("zh")) {
            return QStringLiteral("CN");
        }
        if (c == QLatin1StringView("en")) {
            return QStringLiteral("EN");
        }
        if (c == QLatin1StringView("zh,en")) {
            return QStringLiteral("CN/EN");
        }
        if (c == QLatin1StringView("multilingual")) {
            return QCoreApplication::translate("AsrSettingWidget",
                                               "Multilingual");
        }
        if (c == QLatin1StringView("system")) {
            return QCoreApplication::translate("AsrSettingWidget", "System");
        }
        return c;
    };

    return QStringLiteral("%1 - %2 - %3")
        .arg(jsonString(m, "name"),
             m.value("streamingSupport", false)
                 ? QCoreApplication::translate("AsrSettingWidget", "Real-time")
                 : QCoreApplication::translate("AsrSettingWidget", "Offline"),
             langLabel(jsonString(m, "languages")));
}

} // namespace

// ──────────────────────────────────────────────────────────────────────────
// Construction
// ──────────────────────────────────────────────────────────────────────────

AsrSettingWidget::AsrSettingWidget(QWidget *parent)
    : QWidget(parent), m_ui(std::make_unique<Ui::AsrSettingWidget>())
{
    m_ui->setupUi(this);
    initLlmProviders();
    initLlmPrompt();
    initOcrProvider();
    initLlmChecks();
    initAsrModel();
    initIcons();

    connect(m_ui->hotwordsButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);

    updateUiFromConfig();
}

AsrSettingWidget::~AsrSettingWidget() = default;

void AsrSettingWidget::updateUiFromConfig()
{
    const QString savedLlmProviderId = currentLlmProviderId();
    const int llmProviderIndex =
        m_ui->providerCombo->findData(savedLlmProviderId);
    {
        const QSignalBlocker blocker(m_ui->providerCombo);
        if (llmProviderIndex >= 0) {
            m_ui->providerCombo->setCurrentIndex(llmProviderIndex);
        }
    }

    const auto llmProvider = llmProviderPreset(m_ui->providerCombo);
    if (llmProvider.is_object()) {
        applyLlmProviderToUi(llmProvider);
    }

    refreshPromptLabel();

    const int ocrProviderIndex =
        m_ui->ocrCombo->findData(currentOcrProviderId());
    {
        const QSignalBlocker blocker(m_ui->ocrCombo);
        if (ocrProviderIndex >= 0) {
            m_ui->ocrCombo->setCurrentIndex(ocrProviderIndex);
        }
    }

    {
        const QSignalBlocker llmBlocker(m_ui->llmPostProcessCheck);
        const QSignalBlocker ocrBlocker(m_ui->ocrContextCheck);
        const bool llmPostProcessEnabled =
            appConfigBool("/settings/llm/llmPostProcessEnableForAsr", false);
        m_ui->llmPostProcessCheck->setChecked(llmPostProcessEnabled);
        m_ui->ocrContextCheck->setEnabled(llmPostProcessEnabled);
        m_ui->ocrContextCheck->setChecked(ocrContextEnabledForAsr());
    }

    const QString savedAsrProviderId = currentAsrProviderId();
    int asrProviderIndex = -1;
    for (int i = 0; i < m_ui->modelCombo->count(); ++i) {
        if (m_ui->modelCombo->itemData(i).toString() == savedAsrProviderId) {
            asrProviderIndex = i;
            break;
        }
    }
    {
        const QSignalBlocker blocker(m_ui->modelCombo);
        if (asrProviderIndex >= 0) {
            m_ui->modelCombo->setCurrentIndex(asrProviderIndex);
        }
    }
    onAsrModelChanged(m_ui->modelCombo->currentIndex());
    loadActiveAsrPreset();
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Providers
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initLlmProviders()
{
    auto *combo = m_ui->providerCombo;
    auto *endpointEdit = m_ui->endpointEdit;
    auto *modelCombo = m_ui->llmModelCombo;
    auto *apiKeyEdit = m_ui->apiKeyEdit;

    modelCombo->lineEdit()->setPlaceholderText(
        tr("Model name sent to the LLM service"));

    // Populate — store only the provider ID
    const nlohmann::json presets = llmPresets();
    for (const auto &[key, preset] : presets.items()) {
        if (!preset.is_object()) {
            continue;
        }
        const QString id = jsonString(preset, "id");
        if (!id.isEmpty()) {
            combo->addItem(jsonString(preset, "name"), id);
        }
    }

    // Endpoint edited
    connect(endpointEdit, &QLineEdit::editingFinished, this, [=]() {
        saveLlmSetting(combo, QStringLiteral("endpoint"),
                       endpointEdit->text().trimmed());
        STATUSBAR_INFO("{}", tr("LLM endpoint saved"));
    });

    // Model edited — commit on focus loss or popup selection
    auto saveModel = [=]() {
        saveLlmSetting(combo, QStringLiteral("currentModel"),
                       modelCombo->currentText().trimmed());
        STATUSBAR_INFO("{}", tr("LLM model saved"));
    };
    connect(modelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(modelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });

    // API key edited
    connect(apiKeyEdit, &QLineEdit::editingFinished, this, [=]() {
        saveLlmSetting(combo, QStringLiteral("apiKey"),
                       apiKeyEdit->text().trimmed());
        STATUSBAR_INFO("{}", tr("LLM API key saved"));
    });

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onLlmProviderChanged);
}

void AsrSettingWidget::onLlmProviderChanged(int /*index*/)
{
    auto *combo = m_ui->providerCombo;
    const auto p = llmProviderPreset(combo);
    if (!p.is_object()) {
        return;
    }

    applyLlmProviderToUi(p);
    setCurrentLlmProviderId(llmProviderId(combo));
    STATUSBAR_INFO("{}",
                   tr("LLM provider saved: %1").arg(combo->currentText()));
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Prompt
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::applyLlmProviderToUi(const nlohmann::json &provider)
{
    const QSignalBlocker epBlocker(m_ui->endpointEdit);
    const QSignalBlocker mBlocker(m_ui->llmModelCombo);
    const QSignalBlocker akBlocker(m_ui->apiKeyEdit);

    m_ui->endpointEdit->setText(llmProviderEndpoint(provider));

    const QString currentModel = llmProviderModel(provider);
    m_ui->llmModelCombo->clear();
    for (const auto &m : provider.value("models", nlohmann::json::array())) {
        if (m.is_string()) {
            m_ui->llmModelCombo->addItem(m.get<QString>());
        }
    }
    if (!currentModel.isEmpty() &&
        m_ui->llmModelCombo->findText(currentModel) < 0)
    {
        m_ui->llmModelCombo->addItem(currentModel);
    }
    m_ui->llmModelCombo->setEditText(currentModel);

    m_ui->apiKeyEdit->setText(llmProviderApiKey(provider));
}

void AsrSettingWidget::initLlmPrompt()
{
    connect(m_ui->promptEditButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditPrompt);
}

void AsrSettingWidget::refreshPromptLabel()
{
    const QString text = appConfigString("/settings/llm/userPrompt");
    m_ui->promptLabel->setText(
        QStringLiteral("%1 …").arg(text.simplified().left(50)));
    m_ui->promptLabel->setToolTip(text);
}

void AsrSettingWidget::onEditPrompt()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("User Prompt"));
    dialog.resize(580, 360);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    const QString hint =
        tr("Available variables: {{input}}, {{context}}, {{hotwords}}");
    auto *label = new QLabel(QStringLiteral("<b>%1</b><br><small>%2</small>")
                                 .arg(tr("User Prompt"), hint),
                             &dialog);
    label->setWordWrap(true);
    layout->addWidget(label);

    auto *editor = new QTextEdit(&dialog);
    editor->setAcceptRichText(false);
    editor->setPlaceholderText(
        tr("Use {{input}}, {{context}}, and {{hotwords}} as needed"));
    editor->setPlainText(appConfigString("/settings/llm/userPrompt"));
    layout->addWidget(editor, 1);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString text = editor->toPlainText().trimmed();
    setAppConfigValue("/settings/llm/userPrompt", text);
    refreshPromptLabel();
    STATUSBAR_INFO("{}", tr("LLM prompt saved"));
}

// ──────────────────────────────────────────────────────────────────────────
// OCR Provider
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initOcrProvider()
{
    auto *combo = m_ui->ocrCombo;

    const nlohmann::json presets = ocrPresets();
    if (presets.is_object()) {
        for (const auto &[key, preset] : presets.items()) {
            if (!preset.is_object()) {
                continue;
            }
            combo->addItem(jsonString(preset, "name"),
                           jsonString(preset, "id"));
        }
    }

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onOcrProviderChanged);
}

void AsrSettingWidget::onOcrProviderChanged(int /*index*/)
{
    setCurrentOcrProviderId(m_ui->ocrCombo->currentData().toString());
}

// ──────────────────────────────────────────────────────────────────────────
// Hotwords
// ──────────────────────────────────────────────────────────────────────────

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
        const nlohmann::json hw = appConfigValue("/settings/hotwords");
        QStringList lines;
        if (hw.is_array()) {
            for (const auto &item : hw) {
                if (!item.is_string()) {
                    continue;
                }
                const QString s = item.get<QString>().trimmed();
                if (!s.isEmpty()) {
                    lines.append(s);
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
    STATUSBAR_INFO(
        "{}", tr("Hot words saved, reloading speech recognition model..."));
    loadActiveAsrPreset();
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Polish / OCR Context checkboxes
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initLlmChecks()
{
    auto *llmCheck = m_ui->llmPostProcessCheck;
    auto *ocrCheck = m_ui->ocrContextCheck;

    connect(llmCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAppConfigValue("/settings/llm/llmPostProcessEnableForAsr", checked);
    });
    connect(ocrCheck, &QCheckBox::toggled, this,
            [](bool checked) { setOcrContextEnabledForAsr(checked); });
    connect(llmCheck, &QCheckBox::toggled, ocrCheck, &QCheckBox::setEnabled);
}

// ──────────────────────────────────────────────────────────────────────────
// ASR Model
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initAsrModel()
{
    auto *combo = m_ui->modelCombo;

    const nlohmann::json presets = asrPresets();
    if (presets.is_object()) {
        for (const auto &[key, p] : presets.items()) {
            if (!p.is_object()) {
                continue;
            }
            combo->addItem(asrModelLabel(p), QString::fromStdString(key));
        }
    }

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onAsrModelChanged);
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseAsrModel);
}

void AsrSettingWidget::onAsrModelChanged(int index)
{
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        m_ui->useButton->setEnabled(false);
        return;
    }

    const QString currentConfigAsrProviderId = currentAsrProviderId();
    const QString currentComboItemProviderId =
        m_ui->modelCombo->itemData(index).toString();
    m_ui->useButton->setEnabled(!currentComboItemProviderId.isEmpty() &&
                                currentComboItemProviderId !=
                                    currentConfigAsrProviderId);
}

void AsrSettingWidget::loadActiveAsrPreset()
{
    auto *vc = VoiceInputController::instance();
    if (!vc) {
        return;
    }

    const nlohmann::json preset = talkinput::currentAsrPreset();
    const QString providerId = jsonString(preset, "id");
    if (!providerId.isEmpty()) {
        loadAsrPreset(providerId);
        return;
    }

    vc->unloadSpeechRecognitionModel();
}

void AsrSettingWidget::loadAsrPreset(const QString &providerId)
{
    const nlohmann::json preset = asrPresetById(providerId);
    auto *vc = VoiceInputController::instance();
    if (!vc) {
        return;
    }

    const QString name = jsonString(preset, "name");
    if (!preset.is_object() || name.isEmpty()) {
        vc->unloadSpeechRecognitionModel();
        return;
    }

    if (!isAsrPresetInstalled(preset)) {
        if (m_asrDownloadReply) {
            STATUSBAR_INFO("{}", tr("A model download is already running."));
            return;
        }

        QString error;
        if (!enqueueAsrModelDownloads(providerId, &error)) {
            if (!error.isEmpty()) {
                STATUSBAR_INFO("{}", error);
            }
            return;
        }

        m_requestedAsrModelPointer = asrPresetPointer(providerId);
        startNextAsrModelDownload();
        return;
    }

    SPDLOG_DEBUG("AsrSettingWidget: loading ASR model {}", name);
    vc->loadSpeechRecognitionModel(preset);
    SPDLOG_INFO("ASR model loaded: {} ({})", name, asrModelDir(preset));
}

// ──────────────────────────────────────────────────────────────────────────
// Use / Download
// ──────────────────────────────────────────────────────────────────────────

bool AsrSettingWidget::enqueueAsrModelDownloads(const QString &providerId,
                                                QString *errorMessage)
{
    const QString modelPointer = asrPresetPointer(providerId);
    const nlohmann::json model = appConfigValue(modelPointer.toStdString());
    if (!model.is_object()) {
        if (errorMessage) {
            *errorMessage = tr("Model preset is invalid.");
        }
        return false;
    }

    const QUrl url(jsonString(model, "url"));
    if (url.isEmpty()) {
        if (errorMessage) {
            *errorMessage = tr("Model download URL is empty.");
        }
        return false;
    }

    QDir modelRoot(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!modelRoot.exists() && !modelRoot.mkpath(QStringLiteral("."))) {
        if (errorMessage) {
            *errorMessage = tr("Failed to create model cache directory.");
        }
        return false;
    }

    m_pendingAsrDownloadPointers.clear();
    if (!isAsrPresetInstalled(model)) {
        m_pendingAsrDownloadPointers.enqueue(modelPointer);
    }

    const nlohmann::json punctuationModel =
        model.value("postPunctuationModel", nlohmann::json::object());
    if (punctuationModel.is_object() && !punctuationModel.empty() &&
        !isAsrPresetInstalled(punctuationModel) &&
        !QUrl(jsonString(punctuationModel, "url")).isEmpty())
    {
        m_pendingAsrDownloadPointers.enqueue(
            modelPointer + QStringLiteral("/postPunctuationModel"));
    }

    return !m_pendingAsrDownloadPointers.isEmpty();
}

void AsrSettingWidget::startNextAsrModelDownload()
{
    if (m_asrDownloadReply) {
        STATUSBAR_INFO("{}", tr("A model download is already running."));
        return;
    }

    if (m_pendingAsrDownloadPointers.isEmpty()) {
        onDownloadFinished(m_requestedAsrModelPointer);
        m_requestedAsrModelPointer.clear();
        return;
    }

    m_activeAsrDownloadPointer = m_pendingAsrDownloadPointers.dequeue();
    const nlohmann::json model =
        appConfigValue(m_activeAsrDownloadPointer.toStdString());
    const QUrl url(jsonString(model, "url"));
    const QString modelName = jsonString(model, "name");
    const QString archiveName = QFileInfo(url.path()).fileName();
    const QString modelRoot =
        QDir(appDataDir()).filePath(QStringLiteral("models"));

    m_activeAsrArchivePath = QDir(modelRoot).filePath(archiveName);
    m_activeAsrTempPath = m_activeAsrArchivePath + QStringLiteral(".part");

    QFile::remove(m_activeAsrTempPath);
    m_asrDownloadFile = std::make_unique<QFile>(m_activeAsrTempPath);
    if (!m_asrDownloadFile->open(QIODevice::WriteOnly)) {
        STATUSBAR_INFO("{}", tr("Cannot create ASR model download file."));
        clearAsrModelDownload();
        return;
    }

    if (!m_asrDownloadNetwork) {
        m_asrDownloadNetwork = std::make_unique<QNetworkAccessManager>();
    }

    STATUSBAR_INFO("{}", tr("Downloading ASR model: %1").arg(modelName));
    m_asrDownloadReply = m_asrDownloadNetwork->get(QNetworkRequest(url));
    connect(m_asrDownloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_asrDownloadReply && m_asrDownloadFile) {
            m_asrDownloadFile->write(m_asrDownloadReply->readAll());
        }
    });
    connect(m_asrDownloadReply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) {
                if (total <= 0) {
                    return;
                }
                const int percent = static_cast<int>(received * 100 / total);
                STATUSBAR_INFO("{}",
                               tr("Downloading ASR model %1%...").arg(percent));
            });
    connect(m_asrDownloadReply, &QNetworkReply::finished, this,
            &AsrSettingWidget::onAsrModelDownloadFinished);
}

void AsrSettingWidget::onAsrModelDownloadFinished()
{
    QNetworkReply *reply = m_asrDownloadReply;
    m_asrDownloadReply = nullptr;

    const nlohmann::json model =
        appConfigValue(m_activeAsrDownloadPointer.toStdString());
    const QString modelName = jsonString(model, "name");

    if (m_asrDownloadFile && reply) {
        m_asrDownloadFile->write(reply->readAll());
        m_asrDownloadFile->close();
    }

    const bool failed = !reply || reply->error() != QNetworkReply::NoError;
    const QString errorText = reply ? reply->errorString() : QString();
    if (reply) {
        reply->deleteLater();
    }
    m_asrDownloadFile.reset();

    if (failed) {
        QFile::remove(m_activeAsrTempPath);
        STATUSBAR_INFO("{}",
                       tr("ASR model download failed: %1").arg(errorText));
        clearAsrModelDownload();
        return;
    }

    QFile::remove(m_activeAsrArchivePath);
    if (!QFile::rename(m_activeAsrTempPath, m_activeAsrArchivePath)) {
        STATUSBAR_INFO("{}", tr("Failed to save ASR model download."));
        clearAsrModelDownload();
        return;
    }

    QDir dest(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!dest.exists() && !dest.mkpath(QStringLiteral("."))) {
        STATUSBAR_INFO("{}", tr("Failed to create model directory."));
        clearAsrModelDownload();
        return;
    }

    STATUSBAR_INFO("{}", tr("Extracting ASR model: %1").arg(modelName));
    auto result = extractArchive(m_activeAsrArchivePath, dest.absolutePath());
    QFile::remove(m_activeAsrArchivePath);
    if (!result) {
        STATUSBAR_INFO(
            "{}", tr("ASR model extraction failed: %1").arg(result.error()));
        clearAsrModelDownload();
        return;
    }

    m_activeAsrDownloadPointer.clear();
    m_activeAsrArchivePath.clear();
    m_activeAsrTempPath.clear();
    startNextAsrModelDownload();
}

void AsrSettingWidget::clearAsrModelDownload()
{
    m_asrDownloadFile.reset();
    m_pendingAsrDownloadPointers.clear();
    m_requestedAsrModelPointer.clear();
    m_activeAsrDownloadPointer.clear();
    m_activeAsrArchivePath.clear();
    m_activeAsrTempPath.clear();
}

void AsrSettingWidget::onUseAsrModel()
{
    const int index = m_ui->modelCombo->currentIndex();
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        return;
    }

    const QString providerId = m_ui->modelCombo->itemData(index).toString();
    const nlohmann::json m = asrPresetById(providerId);
    if (!m.is_object()) {
        return;
    }

    setCurrentAsrProviderId(providerId);
    m_ui->useButton->setEnabled(false);
    loadAsrPreset(providerId);
}

void AsrSettingWidget::onDownloadFinished(const QString &modelPointer)
{
    const nlohmann::json m = appConfigValue(modelPointer.toStdString());
    if (!m.is_object()) {
        return;
    }

    const QString modelName = jsonString(m, "name");

    if (currentAsrProviderId() == jsonString(m, "id")) {
        loadAsrPreset(jsonString(m, "id"));
        STATUSBAR_INFO("{}", tr("Speech recognition model loaded: %1")
                                 .arg(jsonString(m, "name")));
    }
    else {
        STATUSBAR_INFO("{}", tr("Downloaded: %1").arg(modelName));
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Icons
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initIcons()
{
    setButtonIcon(m_ui->hotwordsButton, ":/resources/icons/hotwords.svg", 22);
    m_ui->hotwordsButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->promptEditButton, ":/resources/icons/edit.svg", 22);
    m_ui->promptEditButton->setProperty("buttonRole", "icon");
}

// ──────────────────────────────────────────────────────────────────────────
// Events
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_ui->retranslateUi(this);
        onAsrModelChanged(m_ui->modelCombo->currentIndex());
    }
}

} // namespace talkinput
