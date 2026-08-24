#include "recognition_model_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_download.h"
#include "ui_recognition_model_widget.h"
#include "utils.h"
#include "voice_input_controller.h"

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>

namespace talkinput
{

RecognitionModelWidget::RecognitionModelWidget(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    initAsrModel();
    initActiveMode();
    retranslate();
    refreshFromConfig();
}

RecognitionModelWidget::~RecognitionModelWidget() = default;

void RecognitionModelWidget::buildUi()
{
    m_ui = std::make_unique<Ui::RecognitionModelWidget>();
    m_ui->setupUi(this);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_ui->useButton, ":/resources/icons/check.svg", iconSize);
    m_ui->useButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->browserButton, ":/resources/icons/globe.svg", iconSize);
    m_ui->browserButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->importButton, ":/resources/icons/import.svg", iconSize);
    m_ui->importButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->hotwordsSaveButton, ":/resources/icons/save.svg",
                  iconSize);
    m_ui->hotwordsSaveButton->setProperty("buttonRole", "icon");

    connect(m_ui->hotwordsEdit, &QTextEdit::textChanged, this,
            &RecognitionModelWidget::onHotwordsChanged);
    connect(m_ui->hotwordsSaveButton, &QPushButton::clicked, this,
            [this]() { saveHotwords(true); });
}

void RecognitionModelWidget::setRecognitionActions(QAction *startAction,
                                                   QAction *fileAction)
{
    const auto bindAction = [](QPushButton *button, QAction *action) {
        const auto sync = [button, action]() {
            button->setText(action->text());
            button->setIcon(action->icon());
            button->setToolTip(action->toolTip());
            button->setEnabled(action->isEnabled());
        };
        QObject::connect(button, &QPushButton::clicked, action,
                         &QAction::trigger);
        QObject::connect(action, &QAction::changed, button, sync);
        sync();
    };
    bindAction(m_ui->startRecognitionButton, startAction);
    bindAction(m_ui->recognizeFileButton, fileAction);
}

void RecognitionModelWidget::setRecognitionResult(const QString &text)
{
    m_ui->resultEdit->setPlainText(text);
}

void RecognitionModelWidget::retranslate()
{
    m_ui->retranslateUi(this);

    {
        const QSignalBlocker blocker(m_ui->modeCombo);
        const QString current = m_ui->modeCombo->currentData().toString();
        m_ui->modeCombo->clear();
        m_ui->modeCombo->addItem(tr("ASR only"), QStringLiteral("asr_only"));
        m_ui->modeCombo->addItem(tr("ASR + AI Polish"),
                                 QStringLiteral("asr_llm"));
        m_ui->modeCombo->addItem(tr("ASR + OCR context + AI Polish"),
                                 QStringLiteral("asr_llm_ocr"));
        const int idx = m_ui->modeCombo->findData(current);
        if (idx >= 0) {
            m_ui->modeCombo->setCurrentIndex(idx);
        }
    }

    refreshAsrModelCombo();
}

void RecognitionModelWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void RecognitionModelWidget::refreshFromConfig()
{
    auto task =
        useAsrModel(QString::fromStdString(appConfig().settings.asrProviderId));
    updateActiveModeDisplay();

    QStringList lines;
    for (const auto &hw : appConfig().settings.hotwords) {
        const QString s = QString::fromStdString(hw).trimmed();
        if (!s.isEmpty()) {
            lines.append(s);
        }
    }
    const QSignalBlocker blocker(m_ui->hotwordsEdit);
    m_ui->hotwordsEdit->setPlainText(lines.join(QLatin1Char('\n')));
}

void RecognitionModelWidget::initActiveMode()
{
    connect(m_ui->modeCombo, &QComboBox::currentIndexChanged, this, [this]() {
        const QString mode = m_ui->modeCombo->currentData().toString();
        appConfig().settings.activeMode = mode.toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("Active mode changed to %1")
                                 .arg(m_ui->modeCombo->currentText()));
    });
}

void RecognitionModelWidget::updateActiveModeDisplay()
{
    const QString activeMode =
        QString::fromStdString(appConfig().settings.activeMode);
    const int idx = m_ui->modeCombo->findData(activeMode);
    if (idx >= 0) {
        const QSignalBlocker blocker(m_ui->modeCombo);
        m_ui->modeCombo->setCurrentIndex(idx);
    }
}

void RecognitionModelWidget::initAsrModel()
{
    connect(m_ui->useButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onUseAsrModel);
    connect(m_ui->browserButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onOpenModelUrl);
    connect(m_ui->importButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onImportModel);
    refreshAsrModelCombo();
}

void RecognitionModelWidget::loadInstalledAsrModel(const QString &providerId)
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

    SPDLOG_DEBUG("RecognitionModelWidget: loading ASR model {}", preset.name);
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

void RecognitionModelWidget::refreshAsrModelCombo()
{
    const QString currentId =
        QString::fromStdString(appConfig().settings.asrProviderId);
    const auto &presets = appConfig().asrPresets;

    m_ui->modelCombo->clear();

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
        m_ui->modelCombo->addItem(label, providerId);
        ++i;
    }

    if (foundIndex >= 0) {
        const QSignalBlocker blocker(m_ui->modelCombo);
        m_ui->modelCombo->setCurrentIndex(foundIndex);
    }
}

QCoro::Task<bool>
RecognitionModelWidget::downloadAsrModel(const QString &providerId)
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

    const QPointer<RecognitionModelWidget> guard(this);
    auto result = co_await downloadModelArchive(
        QString::fromStdString(model.name), QString::fromStdString(model.url),
        QStringLiteral("ASR"));
    if (!guard) {
        co_return false;
    }
    if (!result.ok) {
        STATUSBAR_INFO("{}",
                       tr("ASR model download failed: %1").arg(result.error));
        co_return false;
    }

    co_return true;
}

void RecognitionModelWidget::onUseAsrModel()
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

QCoro::Task<void> RecognitionModelWidget::useAsrModel(const QString &providerId)
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

void RecognitionModelWidget::onOpenModelUrl()
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

void RecognitionModelWidget::onImportModel()
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

    auto task = useAsrModel(providerId);
}

void RecognitionModelWidget::onHotwordsChanged()
{
    saveHotwords(false);
}

void RecognitionModelWidget::saveHotwords(bool reloadModel)
{
    std::vector<std::string> hwList;
    const QStringList lines =
        m_ui->hotwordsEdit->toPlainText().split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            hwList.push_back(trimmed.toStdString());
        }
    }
    appConfig().settings.hotwords = std::move(hwList);
    markConfigDirty();

    if (reloadModel) {
        STATUSBAR_INFO(
            "{}", tr("Hot words saved, reloading speech recognition model..."));
        auto task = useAsrModel(
            QString::fromStdString(appConfig().settings.asrProviderId));
    }
}

} // namespace talkinput
