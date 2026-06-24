#include "asr_setting_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "ui_asr_setting_widget.h"
#include "utils.h"
#include "voice_input_controller.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QCoro/QCoroNetworkReply>
#include <QCoro/QCoroSignal>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
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
#include <QUrl>
#include <QVBoxLayout>

namespace talkinput
{

namespace
{

// ── Shared helpers ────────────────────────────────────────────────────

QString asrModelLabel(const AsrPreset &m)
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
            return {};
        }
        return c;
    };

    return QStringLiteral("%1 - %2 - %3")
        .arg(QString::fromStdString(m.name),
             m.streamingSupport
                 ? QCoreApplication::translate("AsrSettingWidget", "Real-time")
                 : QCoreApplication::translate("AsrSettingWidget", "Offline"),
             langLabel(QString::fromStdString(m.languages)));
}

bool isModelInstalled(const std::string &modelDirName,
                      const std::map<std::string, std::string> &files)
{
    if (modelDirName.empty()) {
        return false;
    }
    const QString modelDir =
        QDir(appDataDir())
            .filePath(QStringLiteral("models/%1")
                          .arg(QString::fromStdString(modelDirName)));
    if (!QFileInfo(modelDir).isDir()) {
        SPDLOG_INFO("Model directory does not exist: {}", modelDir);
        return false;
    }
    for (const auto &[key, relative] : files) {
        const QFileInfo fi(
            QDir(modelDir).filePath(QString::fromStdString(relative)));
        if (!fi.exists()) {
            SPDLOG_INFO("Model file does not exist: {}", fi.absoluteFilePath());
            return false;
        }
    }
    return true;
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
    initAsrModel();
    initIcons();
    initShortcuts();
    initActiveMode();

    connect(m_ui->hotwordsButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditHotwords);

    connect(m_ui->useClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.useClipboard = checked;
                markConfigDirty();
            });
    connect(m_ui->copyToClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.copyToClipboard = checked;
                markConfigDirty();
            });
    connect(m_ui->restoreClipboardCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.restoreClipboard = checked;
                markConfigDirty();
            });
    connect(m_ui->saveOcrScreenshotCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.saveOcrScreenshot = checked;
                markConfigDirty();
            });
    connect(m_ui->saveAsrAudioCheck, &QCheckBox::toggled, this,
            [](bool checked) {
                appConfig().settings.saveAsrAudio = checked;
                markConfigDirty();
            });

    updateUiFromConfig();
}

AsrSettingWidget::~AsrSettingWidget() = default;

void AsrSettingWidget::updateUiFromConfig()
{
    const QString savedLlmProviderId =
        QString::fromStdString(appConfig().settings.llmProviderId);
    const int llmProviderIndex =
        m_ui->providerCombo->findData(savedLlmProviderId);
    {
        const QSignalBlocker blocker(m_ui->providerCombo);
        if (llmProviderIndex >= 0) {
            m_ui->providerCombo->setCurrentIndex(llmProviderIndex);
        }
    }

    {
        const auto &llmPresets = appConfig().llmPresets;
        auto llmIt = llmPresets.find(
            m_ui->providerCombo->currentData().toString().toStdString());
        if (llmIt != llmPresets.end()) {
            applyLlmProviderToUi(llmIt->second);
        }
    }

    refreshPromptLabel();

    const int ocrProviderIndex = m_ui->ocrCombo->findData(
        QString::fromStdString(appConfig().settings.ocrProviderId));
    {
        const QSignalBlocker blocker(m_ui->ocrCombo);
        if (ocrProviderIndex >= 0) {
            m_ui->ocrCombo->setCurrentIndex(ocrProviderIndex);
        }
    }

    {
        const QSignalBlocker b1(m_ui->triggerHotkeyEdit);
        const QSignalBlocker b2(m_ui->modeSwitchHotkeyEdit);
        const QSignalBlocker b3(m_ui->activeModeCombo);
        m_ui->triggerHotkeyEdit->setKeySequence(QKeySequence(
            QString::fromStdString(appConfig().settings.triggerHotkey)));
        m_ui->modeSwitchHotkeyEdit->setKeySequence(QKeySequence(
            QString::fromStdString(appConfig().settings.modeSwitchHotkey)));
        const QString activeMode =
            QString::fromStdString(appConfig().settings.activeMode);
        const int modeIdx = m_ui->activeModeCombo->findData(activeMode);
        if (modeIdx >= 0) {
            m_ui->activeModeCombo->setCurrentIndex(modeIdx);
        }
    }

    refreshAsrModelCombo();

    {
        const QSignalBlocker bc1(m_ui->useClipboardCheck);
        const QSignalBlocker bc2(m_ui->copyToClipboardCheck);
        const QSignalBlocker bc3(m_ui->restoreClipboardCheck);
        const QSignalBlocker bc4(m_ui->saveOcrScreenshotCheck);
        const QSignalBlocker bc5(m_ui->saveAsrAudioCheck);
        m_ui->useClipboardCheck->setChecked(appConfig().settings.useClipboard);
        m_ui->copyToClipboardCheck->setChecked(
            appConfig().settings.copyToClipboard);
        m_ui->restoreClipboardCheck->setChecked(
            appConfig().settings.restoreClipboard);
        m_ui->saveOcrScreenshotCheck->setChecked(
            appConfig().settings.saveOcrScreenshot);
        m_ui->saveAsrAudioCheck->setChecked(appConfig().settings.saveAsrAudio);
    }

    auto task =
        useAsrModel(QString::fromStdString(appConfig().settings.asrProviderId));
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
    for (const auto &[key, preset] : appConfig().llmPresets) {
        const QString name = QString::fromStdString(preset.name);
        if (!name.isEmpty()) {
            combo->addItem(name, QString::fromStdString(key));
        }
    }

    // Endpoint edited
    connect(endpointEdit, &QLineEdit::editingFinished, this, [=]() {
        auto &preset =
            appConfig()
                .llmPresets[combo->currentData().toString().toStdString()];
        preset.endpoint = endpointEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM endpoint saved"));
    });

    // Model edited — commit on focus loss or popup selection
    auto saveModel = [=]() {
        auto &preset =
            appConfig()
                .llmPresets[combo->currentData().toString().toStdString()];
        preset.currentModel = modelCombo->currentText().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM model saved"));
    };
    connect(modelCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveModel);
    connect(modelCombo, &QComboBox::activated, this,
            [saveModel](int) { saveModel(); });

    // API key edited
    connect(apiKeyEdit, &QLineEdit::editingFinished, this, [=]() {
        auto &preset =
            appConfig()
                .llmPresets[combo->currentData().toString().toStdString()];
        preset.apiKey = apiKeyEdit->text().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("LLM API key saved"));
    });

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onLlmProviderChanged);
}

void AsrSettingWidget::onLlmProviderChanged(int /*index*/)
{
    auto *combo = m_ui->providerCombo;

    const auto &llmPresets = appConfig().llmPresets;
    auto it = llmPresets.find(combo->currentData().toString().toStdString());
    if (it == llmPresets.end()) {
        return;
    }
    const auto &preset = it->second;

    applyLlmProviderToUi(preset);
    appConfig().settings.llmProviderId =
        combo->currentData().toString().toStdString();
    markConfigDirty();
    STATUSBAR_INFO("{}",
                   tr("LLM provider saved: %1").arg(combo->currentText()));
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Prompt
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::applyLlmProviderToUi(const LlmPreset &provider)
{
    const QSignalBlocker epBlocker(m_ui->endpointEdit);
    const QSignalBlocker mBlocker(m_ui->llmModelCombo);
    const QSignalBlocker akBlocker(m_ui->apiKeyEdit);

    m_ui->endpointEdit->setText(QString::fromStdString(provider.endpoint));

    const QString currentModel = QString::fromStdString(provider.currentModel);
    m_ui->llmModelCombo->clear();
    for (const auto &[key, info] : provider.models) {
        m_ui->llmModelCombo->addItem(QString::fromStdString(key),
                                     QString::fromStdString(key));
    }
    if (!currentModel.isEmpty() &&
        m_ui->llmModelCombo->findText(currentModel) < 0)
    {
        m_ui->llmModelCombo->addItem(currentModel, currentModel);
    }
    m_ui->llmModelCombo->setEditText(currentModel);

    m_ui->apiKeyEdit->setText(QString::fromStdString(provider.apiKey));
}

void AsrSettingWidget::initLlmPrompt()
{
    connect(m_ui->promptEditButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onEditPrompt);
}

void AsrSettingWidget::refreshPromptLabel()
{
    const QString text =
        QString::fromStdString(appConfig().settings.llmUserPrompt);
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
    editor->setPlainText(
        QString::fromStdString(appConfig().settings.llmUserPrompt));
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
    appConfig().settings.llmUserPrompt = text.toStdString();
    markConfigDirty();
    refreshPromptLabel();
    STATUSBAR_INFO("{}", tr("LLM prompt saved"));
}

// ──────────────────────────────────────────────────────────────────────────
// OCR Provider
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initOcrProvider()
{
    auto *combo = m_ui->ocrCombo;

    for (const auto &[key, preset] : appConfig().ocrPresets) {
        combo->addItem(QString::fromStdString(preset.name),
                       QString::fromStdString(key));
    }

    connect(combo, &QComboBox::currentIndexChanged, this,
            &AsrSettingWidget::onOcrProviderChanged);
}

void AsrSettingWidget::onOcrProviderChanged(int /*index*/)
{
    appConfig().settings.ocrProviderId =
        m_ui->ocrCombo->currentData().toString().toStdString();
    markConfigDirty();

    if (auto *vc = VoiceInputController::instance()) {
        vc->reloadOcrRecognizer();
    }
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
        QStringList lines;
        for (const auto &hw : appConfig().settings.hotwords) {
            const QString s = QString::fromStdString(hw).trimmed();
            if (!s.isEmpty()) {
                lines.append(s);
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

    std::vector<std::string> hwList;
    const QStringList lines =
        editor->toPlainText().trimmed().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            hwList.push_back(trimmed.toStdString());
        }
    }
    appConfig().settings.hotwords = std::move(hwList);
    markConfigDirty();
    STATUSBAR_INFO(
        "{}", tr("Hot words saved, reloading speech recognition model..."));
    auto task =
        useAsrModel(QString::fromStdString(appConfig().settings.asrProviderId));
}

// ──────────────────────────────────────────────────────────────────────────
// LLM Polish / OCR Context checkboxes
// ──────────────────────────────────────────────────────────────────────────

// ──────────────────────────────────────────────────────────────────────────
// ASR Model
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initAsrModel()
{
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onUseAsrModel);
    connect(m_ui->browserButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onOpenModelUrl);
    connect(m_ui->importButton, &QPushButton::clicked, this,
            &AsrSettingWidget::onImportModel);
    refreshAsrModelCombo();
}

// ──────────────────────────────────────────────────────────────────────────
// ASR Model Loading
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::loadInstalledAsrModel(const QString &providerId)
{
    const auto &presets = appConfig().asrPresets;
    auto it = presets.find(providerId.toStdString());
    if (it == presets.end()) {
        return;
    }
    const auto &preset = it->second;

    if (!isModelInstalled(preset.modelDirName, preset.files)) {
        STATUSBAR_INFO(
            "{}", tr("Model not installed: %1").arg(asrModelLabel(preset)));
        return;
    }

    auto *vc = VoiceInputController::instance();
    if (!vc) {
        return;
    }

    SPDLOG_DEBUG("AsrSettingWidget: loading ASR model {}", preset.name);
    vc->loadSpeechRecognitionModel(preset);
    if (vc->isSpeechRecognitionModelLoaded()) {
        STATUSBAR_INFO("{}",
                       tr("ASR model loaded: %1").arg(asrModelLabel(preset)));
    }
    else {
        STATUSBAR_INFO(
            "{}", tr("ASR model load failed: %1").arg(asrModelLabel(preset)));
    }
}

void AsrSettingWidget::refreshAsrModelCombo()
{
    auto *combo = m_ui->modelCombo;
    const QString currentId =
        QString::fromStdString(appConfig().settings.asrProviderId);
    const auto &presets = appConfig().asrPresets;

    combo->clear();

    int foundIndex = -1;
    int i = 0;
    for (const auto &[key, p] : presets) {
        const QString providerId = QString::fromStdString(key);
        QString label = asrModelLabel(p);
        if (providerId == currentId) {
            label += tr(" (Using)");
            foundIndex = i;
        }
        else if (isModelInstalled(p.modelDirName, p.files)) {
            label += tr(" (Installed)");
        }
        else {
            label += tr(" (Not Installed)");
        }
        combo->addItem(label, providerId);
        ++i;
    }

    if (foundIndex >= 0) {
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(foundIndex);
    }
}

QCoro::Task<bool> AsrSettingWidget::downloadAsrModel(const QString &providerId)
{
    const auto &presetsModel = appConfig().asrPresets;
    auto it = presetsModel.find(providerId.toStdString());
    if (it == presetsModel.end() || it->second.url.empty()) {
        STATUSBAR_INFO("{}", tr("Model preset is invalid."));
        co_return false;
    }
    const auto &model = it->second;

    if (isModelInstalled(model.modelDirName, model.files)) {
        co_return true;
    }

    QDir modelRoot(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!modelRoot.exists() && !modelRoot.mkpath(QStringLiteral("."))) {
        STATUSBAR_INFO("{}", tr("Failed to create model cache directory."));
        co_return false;
    }

    const QUrl url(QString::fromStdString(model.url));
    const QString modelName = QString::fromStdString(model.name);
    const QString archiveName = QFileInfo(url.path()).fileName();
    const QString archivePath = modelRoot.filePath(archiveName);

    const QPointer<AsrSettingWidget> guard(this);

    QNetworkAccessManager manager;
    manager.setTransferTimeout(600000);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = manager.get(request);
    int lastPct = -1;
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, modelName, guard, &lastPct](qint64 received, qint64 total) {
                if (!guard) {
                    return;
                }
                if (total > 0) {
                    int pct = static_cast<int>(received * 100 / total);
                    if (pct == lastPct) {
                        return;
                    }
                    lastPct = pct;
                    STATUSBAR_INFO("{}", tr("Downloading ASR model: %1 … %2%")
                                             .arg(modelName)
                                             .arg(pct));
                }
            });
    auto result = co_await reply;
    reply->setParent(nullptr);

    if (!guard) {
        co_return false;
    }

    bool dlOk = (reply->error() == QNetworkReply::NoError);
    QString dlError = reply->errorString();

    if (dlOk) {
        QFile file(archivePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
        }
        else {
            dlOk = false;
            dlError = file.errorString();
        }
    }
    reply->deleteLater();

    if (!dlOk) {
        STATUSBAR_INFO("{}", tr("ASR model download failed: %1").arg(dlError));
        co_return false;
    }

    STATUSBAR_INFO("{}", tr("Extracting ASR model: %1").arg(modelName));
    auto extractResult = extractArchive(archivePath, modelRoot.absolutePath());
    QFile::remove(archivePath);
    if (!extractResult) {
        STATUSBAR_INFO(
            "{}",
            tr("ASR model extraction failed: %1").arg(extractResult.error()));
        co_return false;
    }

    co_return true;
}

void AsrSettingWidget::onUseAsrModel()
{
    const int index = m_ui->modelCombo->currentIndex();
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        return;
    }

    const QString providerId = m_ui->modelCombo->itemData(index).toString();
    if (providerId.isEmpty()) {
        return;
    }

    auto task = useAsrModel(providerId);
}

QCoro::Task<void> AsrSettingWidget::useAsrModel(const QString &providerId)
{
    // Check if this model is already loaded
    auto *vc = VoiceInputController::instance();
    if (vc && vc->isSpeechRecognitionModelLoaded()) {
        const std::string currentId = vc->loadedPresetId();
        if (!currentId.empty() && currentId == providerId.toStdString()) {
            const auto result = QMessageBox::question(
                this, tr("Model Already Loaded"),
                tr("This model is already loaded. Do you want to reload it?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (result != QMessageBox::Yes) {
                co_return;
            }
        }
    }

    if (!co_await downloadAsrModel(providerId)) {
        co_return;
    }

    appConfig().settings.asrProviderId = providerId.toStdString();
    markConfigDirty();
    loadInstalledAsrModel(providerId);
    refreshAsrModelCombo();
}

void AsrSettingWidget::onOpenModelUrl()
{
    const int index = m_ui->modelCombo->currentIndex();
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        return;
    }

    const QString providerId = m_ui->modelCombo->itemData(index).toString();
    if (providerId.isEmpty()) {
        return;
    }

    const auto &presets = appConfig().asrPresets;
    auto it = presets.find(providerId.toStdString());
    if (it == presets.end() || it->second.url.empty()) {
        STATUSBAR_INFO("{}", tr("No download URL for this model."));
        return;
    }

    QDesktopServices::openUrl(QUrl(QString::fromStdString(it->second.url)));
}

void AsrSettingWidget::onImportModel()
{
    const int index = m_ui->modelCombo->currentIndex();
    if (index < 0 || index >= m_ui->modelCombo->count()) {
        return;
    }

    const QString providerId = m_ui->modelCombo->itemData(index).toString();
    if (providerId.isEmpty()) {
        return;
    }

    const auto &presets = appConfig().asrPresets;
    auto it = presets.find(providerId.toStdString());
    if (it == presets.end() || it->second.url.empty()) {
        STATUSBAR_INFO("{}", tr("No download URL for this model."));
        return;
    }
    const auto &model = it->second;
    const QUrl url(QString::fromStdString(model.url));
    const QString expectedName = QFileInfo(url.path()).fileName();
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("Import Model Archive"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tr("Archives (%1);;All files (*)").arg(expectedName));

    if (filePath.isEmpty()) {
        return;
    }

    const QString actualName = QFileInfo(filePath).fileName();
    if (actualName != expectedName) {
        QMessageBox::warning(
            this, tr("Invalid File"),
            tr("The selected file must be named:\n%1\n\nSelected:\n%2")
                .arg(expectedName, actualName));
        return;
    }

    QDir modelRoot(QDir(appDataDir()).filePath(QStringLiteral("models")));
    if (!modelRoot.exists() && !modelRoot.mkpath(QStringLiteral("."))) {
        STATUSBAR_INFO("{}", tr("Failed to create model cache directory."));
        return;
    }

    const QString destPath = modelRoot.filePath(expectedName);
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }

    if (!QFile::copy(filePath, destPath)) {
        STATUSBAR_INFO("{}", tr("Failed to import model archive."));
        return;
    }

    const QString modelName = QString::fromStdString(model.name);
    STATUSBAR_INFO("{}", tr("Extracting ASR model: %1").arg(modelName));
    auto result = extractArchive(destPath, modelRoot.absolutePath());
    QFile::remove(destPath);
    if (!result) {
        STATUSBAR_INFO(
            "{}", tr("ASR model extraction failed: %1").arg(result.error()));
        return;
    }

    STATUSBAR_INFO("{}", tr("ASR model imported: %1").arg(modelName));

    useAsrModel(providerId);
}

// ──────────────────────────────────────────────────────────────────────────
// Icons
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::initIcons()
{
    const int iconSize = m_ui->useButton->fontMetrics().height();
    setButtonIcon(m_ui->useButton, ":/resources/icons/check.svg", iconSize);
    m_ui->useButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->browserButton, ":/resources/icons/globe.svg", iconSize);
    m_ui->browserButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->importButton, ":/resources/icons/import.svg", iconSize);
    m_ui->importButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->hotwordsButton, ":/resources/icons/hotwords.svg",
                  iconSize);
    m_ui->hotwordsButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->promptEditButton, ":/resources/icons/edit.svg",
                  iconSize);
    m_ui->promptEditButton->setProperty("buttonRole", "icon");
}

void AsrSettingWidget::initShortcuts()
{
    // Trigger hotkey — save and re-register
    auto applyTrigger = [this]() {
        appConfig().settings.triggerHotkey =
            m_ui->triggerHotkeyEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoiceInputController::instance()) {
            ctrl->reregisterTriggerHotkey();
        }
        STATUSBAR_INFO("{}",
                       QCoreApplication::translate("AsrSettingWidget",
                                                   "Trigger shortcut applied"));
    };
    connect(m_ui->triggerHotkeyApplyBtn, &QPushButton::clicked, this,
            applyTrigger);

    // Mode-switch hotkey — save and re-register
    auto applyModeSwitch = [this]() {
        appConfig().settings.modeSwitchHotkey =
            m_ui->modeSwitchHotkeyEdit->keySequence().toString().toStdString();
        markConfigDirty();
        if (auto *ctrl = VoiceInputController::instance()) {
            ctrl->reregisterModeSwitchHotkey();
        }
        STATUSBAR_INFO(
            "{}", QCoreApplication::translate("AsrSettingWidget",
                                              "Mode switch shortcut applied"));
    };
    connect(m_ui->modeSwitchHotkeyApplyBtn, &QPushButton::clicked, this,
            applyModeSwitch);
}

void AsrSettingWidget::initActiveMode()
{
    auto *combo = m_ui->activeModeCombo;
    combo->addItem(tr("ASR only"), QStringLiteral("asr_only"));
    combo->addItem(tr("ASR + AI Polish"), QStringLiteral("asr_llm"));
    combo->addItem(tr("ASR + OCR context + AI Polish"),
                   QStringLiteral("asr_llm_ocr"));

    const QString activeMode =
        QString::fromStdString(appConfig().settings.activeMode);
    const int idx = combo->findData(activeMode);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }

    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo]() {
        const QString mode = combo->currentData().toString();
        appConfig().settings.activeMode = mode.toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}",
                       QCoreApplication::translate("AsrSettingWidget",
                                                   "Active mode changed to %1")
                           .arg(combo->currentText()));
    });
}

void AsrSettingWidget::updateActiveModeDisplay()
{
    auto *combo = m_ui->activeModeCombo;
    const QString activeMode =
        QString::fromStdString(appConfig().settings.activeMode);
    const int idx = combo->findData(activeMode);
    if (idx >= 0) {
        combo->setCurrentIndex(idx);
    }
}

// ──────────────────────────────────────────────────────────────────────────
// Events
// ──────────────────────────────────────────────────────────────────────────

void AsrSettingWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        m_ui->retranslateUi(this);
    }
}

} // namespace talkinput
