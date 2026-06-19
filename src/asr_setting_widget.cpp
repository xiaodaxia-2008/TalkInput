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

nlohmann::json llmProviderPreset(const QComboBox *combo)
{
    const QString id = llmProviderId(combo);
    if (id.isEmpty()) return {};
    return appConfigValue(("/llmPresets/" + id).toStdString());
}

void saveLlmSetting(const QComboBox *combo, const QString &key,
                    const QString &value)
{
    const QString id = llmProviderId(combo);
    if (id.isEmpty()) return;
    setAppConfigValue(
        QStringLiteral("/llmPresets/%1/%2").arg(id, key).toStdString(), value);
}

void applyProviderToUi(const nlohmann::json &provider, QLineEdit *endpointEdit,
                       QComboBox *modelCombo, QLineEdit *apiKeyEdit)
{
    const QSignalBlocker epBlocker(endpointEdit);
    const QSignalBlocker mBlocker(modelCombo);
    const QSignalBlocker akBlocker(apiKeyEdit);

    endpointEdit->setText(jsonString(provider, "endpoint").trimmed());

    const QString currentModel = jsonString(provider, "currentModel").trimmed();
    modelCombo->clear();
    for (const auto &m : provider.value("models", nlohmann::json::array())) {
        if (m.is_string()) modelCombo->addItem(m.get<QString>());
    }
    if (!currentModel.isEmpty() && modelCombo->findText(currentModel) < 0) {
        modelCombo->addItem(currentModel);
    }
    modelCombo->setEditText(currentModel);

    apiKeyEdit->setText(jsonString(provider, "apiKey"));
}

QString asrModelLabel(const nlohmann::json &m)
{
    auto langLabel = [](const QString &c) -> QString {
        if (c == QLatin1StringView("zh")) return QStringLiteral("CN");
        if (c == QLatin1StringView("en")) return QStringLiteral("EN");
        if (c == QLatin1StringView("zh,en")) return QStringLiteral("CN/EN");
        if (c == QLatin1StringView("multilingual"))
            return QCoreApplication::translate("AsrSettingWidget",
                                               "Multilingual");
        if (c == QLatin1StringView("system"))
            return QCoreApplication::translate("AsrSettingWidget", "System");
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
    m_networkManager = new QNetworkAccessManager(this);

    initLlmProviders();
    initPrompt();
    initOcrProvider();
    initLlmChecks();
    initAsrModel();
    initIcons();

    connect(m_ui->hotwordsButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);
}

AsrSettingWidget::~AsrSettingWidget() = default;

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

    // Populate — store only the provider ID, same as OCR combo
    const nlohmann::json presets = appConfigValue("/llmPresets");
    for (const auto &[key, preset] : presets.items()) {
        if (!preset.is_object()) continue;
        const QString id = jsonString(preset, "id");
        if (!id.isEmpty())
            combo->addItem(jsonString(preset, "name"), id);
    }

    // Provider changed → apply UI + persist selection
    connect(combo, &QComboBox::currentIndexChanged, this, [=]() {
        const auto p = llmProviderPreset(combo);
        if (!p.is_object()) return;
        applyProviderToUi(p, endpointEdit, modelCombo, apiKeyEdit);
        setAppConfigValue("/settings/llm/providerId",
                          llmProviderId(combo).toStdString());
        spdlog::get("statusbar")->info(
            "{}",
            tr("LLM provider saved: %1").arg(combo->currentText()));
    });

    // Endpoint edited
    connect(endpointEdit, &QLineEdit::editingFinished, this, [=]() {
        saveLlmSetting(combo, QStringLiteral("endpoint"),
                       endpointEdit->text().trimmed());
        spdlog::get("statusbar")->info("{}", tr("LLM endpoint saved"));
    });

    // Model edited — commit on focus loss or popup selection
    auto saveModel = [=]() {
        saveLlmSetting(combo, QStringLiteral("currentModel"),
                       modelCombo->currentText().trimmed());
        spdlog::get("statusbar")->info("{}", tr("LLM model saved"));
    };
    connect(modelCombo->lineEdit(), &QLineEdit::editingFinished, this, saveModel);
    connect(modelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });

    // API key edited
    connect(apiKeyEdit, &QLineEdit::editingFinished, this, [=]() {
        saveLlmSetting(combo, QStringLiteral("apiKey"),
                       apiKeyEdit->text().trimmed());
        spdlog::get("statusbar")->info("{}", tr("LLM API key saved"));
    });

    // Restore saved provider
    const QString savedId = appConfigString("/settings/llm/providerId");
    const int idx = combo->findData(savedId);
    if (idx >= 0) combo->setCurrentIndex(idx);

    const auto p = llmProviderPreset(combo);
    if (p.is_object())
        applyProviderToUi(p, endpointEdit, modelCombo, apiKeyEdit);
}

// ──────────────────────────────────────────────────────────────────────────
// Prompt
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initPrompt()
{
    connect(m_ui->promptEditButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditPrompt);

    const QString text = appConfigString("/settings/llm/userPrompt");
    m_ui->promptLabel->setText(
        QString("%1 \342\200\246").arg(text.simplified().left(50)));
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
    auto *label =
        new QLabel(QStringLiteral("<b>%1</b><br><small>%2</small>")
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

    if (dialog.exec() != QDialog::Accepted) return;

    const QString text = editor->toPlainText().trimmed();
    setAppConfigValue("/settings/llm/userPrompt", text);
    m_ui->promptLabel->setText(
        QString("%1 \342\200\246").arg(text.simplified().left(50)));
    m_ui->promptLabel->setToolTip(text);
    spdlog::get("statusbar")->info("{}", tr("LLM prompt saved"));
}

// ──────────────────────────────────────────────────────────────────────────
// OCR Provider
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initOcrProvider()
{
    auto *combo = m_ui->ocrCombo;

    const nlohmann::json presets = appConfigValue("/ocrPresets");
    if (presets.is_object()) {
        for (const auto &[key, preset] : presets.items()) {
            if (!preset.is_object()) continue;
            combo->addItem(jsonString(preset, "name"), jsonString(preset, "id"));
        }
    }

    const QString savedId = appConfigString("/settings/ocr/providerId");
    const int idx = combo->findData(savedId);
    if (idx >= 0) combo->setCurrentIndex(idx);

    connect(combo, &QComboBox::currentIndexChanged, this, [combo](int) {
        setAppConfigValue("/settings/ocr/providerId",
                          combo->currentData().toString());
    });
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
        const nlohmann::json hw = appConfigValue("/settings/hotwords");
        QStringList lines;
        if (hw.is_array()) {
            for (const auto &item : hw) {
                if (!item.is_string()) continue;
                const QString s = item.get<QString>().trimmed();
                if (!s.isEmpty()) lines.append(s);
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
        if (!trimmed.isEmpty()) arr.push_back(trimmed.toStdString());
    }
    setAppConfigValue("/settings/hotwords", std::move(arr));
    spdlog::get("statusbar")->info("{}", tr("Hot words saved"));
    emit hotwordsChanged();
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Polish / OCR Context checkboxes
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initLlmChecks()
{
    auto *llmCheck = m_ui->llmPostProcessCheck;
    auto *ocrCheck = m_ui->ocrContextCheck;

    llmCheck->setChecked(
        appConfigBool("/settings/llm/llmPostProcessEnableForAsr", false));
    ocrCheck->setEnabled(llmCheck->isChecked());
    ocrCheck->setChecked(
        appConfigBool("/settings/ocr/ocrContextEnableForAsr", false));

    connect(llmCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAppConfigValue("/settings/llm/llmPostProcessEnableForAsr", checked);
    });
    connect(ocrCheck, &QCheckBox::toggled, this, [](bool checked) {
        setAppConfigValue("/settings/ocr/ocrContextEnableForAsr", checked);
    });
    connect(llmCheck, &QCheckBox::toggled, ocrCheck, &QCheckBox::setEnabled);
}

// ──────────────────────────────────────────────────────────────────────────
// ASR Model
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initAsrModel()
{
    auto *combo = m_ui->modelCombo;

    const nlohmann::json presets = appConfigValue("/asrPresets");
    if (presets.is_object()) {
        for (const auto &[key, p] : presets.items()) {
            if (!p.is_object()) continue;
            combo->addItem(
                asrModelLabel(p),
                QStringLiteral("/asrPresets/%1")
                    .arg(QString::fromStdString(key)));
        }
    }

    // Restore saved selection
    m_activeAsrId = appConfigString("/settings/asr/providerId");
    int restoreIdx = -1;
    if (!m_activeAsrId.isEmpty()) {
        for (int i = 0; i < combo->count(); ++i) {
            const nlohmann::json m = appConfigValue(
                combo->itemData(i).toString().toStdString());
            if (jsonString(m, "id") == m_activeAsrId) {
                restoreIdx = i;
                break;
            }
        }
    }
    if (restoreIdx >= 0) combo->setCurrentIndex(restoreIdx);

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onModelChanged);
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseCurrent);

    if (combo->count() > 0) onModelChanged(combo->currentIndex());
}

void AsrSettingWidget::onModelChanged(int index)
{
    if (index < 0 || index >= m_ui->modelCombo->count()) return;

    const nlohmann::json m =
        appConfigValue(m_ui->modelCombo->itemData(index).toString().toStdString());
    if (!m.is_object()) return;

    const QString modelId = jsonString(m, "id");
    const bool isActive = (modelId == m_activeAsrId);

    QString label = asrModelLabel(m);
    if (isActive) {
        label += QStringLiteral(" (%1)").arg(
            QCoreApplication::translate("AsrSettingWidget", "Activated"));
    }
    m_ui->modelCombo->setItemText(index, label);
    m_ui->useButton->setEnabled(!isActive);
}

void AsrSettingWidget::refreshStatus()
{
    if (m_ui->modelCombo->count() > 0)
        onModelChanged(m_ui->modelCombo->currentIndex());
}

QString AsrSettingWidget::currentModelPointer() const
{
    const int ci = m_ui->modelCombo->currentIndex();
    if (ci < 0) return {};
    return m_ui->modelCombo->currentData().toString();
}

// ──────────────────────────────────────────────────────────────────────────
// Use / Download
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::onUseCurrent()
{
    const QString ptr = currentModelPointer();
    const nlohmann::json m = appConfigValue(ptr.toStdString());
    if (!m.is_object()) return;

    const QString modelId = jsonString(m, "id");
    const bool isSystem =
        jsonString(m, "type") == QLatin1StringView("System");

    if (!isSystem && !isInstalled(m)) {
        // Queue download, then load on completion
        const QUrl url(jsonString(m, "url"));
        if (url.isEmpty()) return;

        QDir cache(appDataDir() + QStringLiteral("/models"));
        if (!cache.exists() && !cache.mkpath(QStringLiteral("."))) return;

        m_downloadTargetPointer = ptr;
        m_downloadQueue.clear();
        m_downloadQueue.enqueue(ptr);

        const nlohmann::json punct =
            m.value("postPunctuationModel", nlohmann::json::object());
        if (punct.is_object() && !punct.empty() && !isInstalled(punct) &&
            !QUrl(jsonString(punct, "url")).isEmpty()) {
            m_downloadQueue.enqueue(
                ptr + QStringLiteral("/postPunctuationModel"));
        }

        m_activeAsrId = modelId;
        spdlog::get("statusbar")->info(
            "{}", tr("Downloading %1...").arg(jsonString(m, "name")));
        startModelDownload(m_downloadQueue.dequeue());
        return;
    }

    // Already installed — load directly
    auto *vc = VoiceInputController::instance();
    if (vc) vc->loadModel(m);
    m_activeAsrId = modelId;
    refreshStatus();
    spdlog::get("statusbar")->info(
        "{}", tr("Model loaded: %1").arg(jsonString(m, "name")));
}

bool AsrSettingWidget::isInstalled(const nlohmann::json &model) const
{
    if (!model.is_object()) return false;
    const QString type = jsonString(model, "type");
    if (type.isEmpty() || type == QLatin1StringView("System")) return true;
    const QString dirName = jsonString(model, "modelDirName");
    if (dirName.isEmpty()) return false;

    const QString path = appDataDir() + QStringLiteral("/models/") + dirName;
    return QFileInfo(path).isDir();
}

void AsrSettingWidget::startModelDownload(const QString &modelPointer)
{
    const nlohmann::json m = appConfigValue(modelPointer.toStdString());
    if (!m.is_object()) return;

    const QUrl url(jsonString(m, "url"));
    const QString modelName = jsonString(m, "name");
    const QString archiveName = QFileInfo(url.path()).fileName();

    m_activeDownloadPath =
        appDataDir() + QStringLiteral("/models/") + archiveName;
    m_activeDownloadTempPath = m_activeDownloadPath + QStringLiteral(".part");
    m_downloadTargetPointer = modelPointer;

    QFile::remove(m_activeDownloadTempPath);
    m_activeDownloadFile = std::make_unique<QFile>(m_activeDownloadTempPath);
    if (!m_activeDownloadFile->open(QIODevice::WriteOnly)) {
        m_downloadTargetPointer.clear();
        spdlog::get("statusbar")->info(
            "{}", tr("Failed to download %1").arg(modelName));
        return;
    }

    QNetworkRequest request(url);
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

    const nlohmann::json m = appConfigValue(modelPointer.toStdString());
    const QString modelName = jsonString(m, "name");

    QDir dest(appDataDir() + QStringLiteral("/models"));
    if (!dest.exists() && !dest.mkpath(QStringLiteral("."))) {
        spdlog::get("statusbar")->info("{}", tr("Extraction failed"));
        return;
    }

    spdlog::get("statusbar")->info("{}", tr("Extracting..."));
    QCoreApplication::processEvents();

    auto result = extractArchive(m_activeDownloadPath, dest.absolutePath());
    QFile::remove(m_activeDownloadPath);

    if (!result) {
        QMessageBox::warning(
            this, tr("Extraction failed"),
            tr("Failed:\n%1").arg(result.error()));
        spdlog::get("statusbar")->info("{}", tr("Extraction failed"));
        return;
    }

    if (!m_downloadQueue.isEmpty()) {
        startModelDownload(m_downloadQueue.dequeue());
        return;
    }

    refreshStatus();

    if (m_activeAsrId == jsonString(m, "id")) {
        auto *vc = VoiceInputController::instance();
        if (vc) vc->loadModel(m);
        spdlog::get("statusbar")->info(
            "{}", tr("Model loaded: %1").arg(modelName));
    }
    else {
        spdlog::get("statusbar")->info(
            "{}", tr("Downloaded: %1").arg(modelName));
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
        refreshStatus();
    }
}

} // namespace talkinput
