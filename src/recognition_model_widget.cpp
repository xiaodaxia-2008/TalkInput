#include "recognition_model_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_download.h"
#include "utils.h"
#include "voice_input_controller.h"

#include <QComboBox>
#include <QAction>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
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
    retranslate();
    refreshFromConfig();
}

RecognitionModelWidget::~RecognitionModelWidget() = default;

void RecognitionModelWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    auto *actionsRow = new QHBoxLayout;
    m_startRecognitionButton = new QPushButton(content);
    m_recognizeFileButton = new QPushButton(content);
    actionsRow->addWidget(m_startRecognitionButton);
    actionsRow->addWidget(m_recognizeFileButton);
    actionsRow->addStretch();
    contentLayout->addLayout(actionsRow);

    // ── ASR model group ────────────────────────────────────────────
    m_modelGroup = new QGroupBox(content);
    auto *modelLayout = new QVBoxLayout(m_modelGroup);
    modelLayout->setContentsMargins(16, 20, 16, 14);
    modelLayout->setSpacing(10);

    auto *modelRow = new QHBoxLayout;
    modelRow->setSpacing(8);
    m_modelLabel = new QLabel(m_modelGroup);
    modelRow->addWidget(m_modelLabel);

    m_modelCombo = new QComboBox(m_modelGroup);
    m_modelCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    modelRow->addWidget(m_modelCombo, 1);

    m_browserButton = new QPushButton(m_modelGroup);
    m_browserButton->setFlat(true);
    modelRow->addWidget(m_browserButton);

    m_importButton = new QPushButton(m_modelGroup);
    m_importButton->setFlat(true);
    modelRow->addWidget(m_importButton);

    m_useButton = new QPushButton(m_modelGroup);
    m_useButton->setFlat(true);
    modelRow->addWidget(m_useButton);

    modelLayout->addLayout(modelRow);

    contentLayout->addWidget(m_modelGroup);

    // ── Hot words group ────────────────────────────────────────────
    m_hotwordsGroup = new QGroupBox(content);
    auto *hotwordsLayout = new QVBoxLayout(m_hotwordsGroup);
    hotwordsLayout->setContentsMargins(16, 20, 16, 14);
    hotwordsLayout->setSpacing(10);

    auto *hintRow = new QHBoxLayout;
    hintRow->setSpacing(8);
    auto *hintIcon = new QLabel(m_hotwordsGroup);
    hintIcon->setObjectName(QStringLiteral("hotwordsHintIcon"));
    hintIcon->setText(QStringLiteral("💡"));
    hintIcon->setAlignment(Qt::AlignCenter);
    hintIcon->setFixedSize(20, 20);
    hintRow->addWidget(hintIcon);

    m_hotwordsHintLabel = new QLabel(m_hotwordsGroup);
    m_hotwordsHintLabel->setObjectName(QStringLiteral("hotwordsHintLabel"));
    m_hotwordsHintLabel->setWordWrap(true);
    hintRow->addWidget(m_hotwordsHintLabel, 1);
    hotwordsLayout->addLayout(hintRow);

    auto *hotwordsButtonRow = new QHBoxLayout;
    m_hotwordsButton = new QPushButton(m_hotwordsGroup);
    m_hotwordsButton->setFlat(true);
    hotwordsButtonRow->addWidget(m_hotwordsButton);
    hotwordsButtonRow->addStretch();
    hotwordsLayout->addLayout(hotwordsButtonRow);

    contentLayout->addWidget(m_hotwordsGroup);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_useButton, ":/resources/icons/check.svg", iconSize);
    m_useButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_browserButton, ":/resources/icons/globe.svg", iconSize);
    m_browserButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_importButton, ":/resources/icons/import.svg", iconSize);
    m_importButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_hotwordsButton, ":/resources/icons/hotwords.svg", iconSize);
    m_hotwordsButton->setProperty("buttonRole", "icon");

    connect(m_hotwordsButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onEditHotwords);
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
    bindAction(m_startRecognitionButton, startAction);
    bindAction(m_recognizeFileButton, fileAction);
}

void RecognitionModelWidget::retranslate()
{
    m_modelGroup->setTitle(tr("Speech Recognition Model"));
    m_hotwordsGroup->setTitle(tr("Hot Words"));
    m_modelLabel->setText(tr("Model:"));

    m_browserButton->setToolTip(tr("Open download page in browser"));
    m_importButton->setToolTip(tr("Import downloaded model archive"));
    m_useButton->setToolTip(tr("Use this model"));
    m_hotwordsButton->setText(tr("Edit Hot Words…"));
    m_hotwordsButton->setToolTip(tr("Edit hot words"));
    m_hotwordsHintLabel->setText(
        tr("One hot word per line. Saved hot words are applied by reloading "
           "the speech recognition model."));

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
}

void RecognitionModelWidget::initAsrModel()
{
    connect(m_useButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onUseAsrModel);
    connect(m_browserButton, &QPushButton::clicked, this,
            &RecognitionModelWidget::onOpenModelUrl);
    connect(m_importButton, &QPushButton::clicked, this,
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

    m_modelCombo->clear();

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
        m_modelCombo->addItem(label, providerId);
        ++i;
    }

    if (foundIndex >= 0) {
        const QSignalBlocker blocker(m_modelCombo);
        m_modelCombo->setCurrentIndex(foundIndex);
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
    const int index = m_modelCombo->currentIndex();
    if (index < 0 || index >= m_modelCombo->count()) {
        return;
    }

    const QString providerId = m_modelCombo->itemData(index).toString();
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
    const int index = m_modelCombo->currentIndex();
    if (index < 0 || index >= m_modelCombo->count()) {
        return;
    }

    const QString providerId = m_modelCombo->itemData(index).toString();
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
    const int index = m_modelCombo->currentIndex();
    if (index < 0 || index >= m_modelCombo->count()) {
        return;
    }

    const QString providerId = m_modelCombo->itemData(index).toString();
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

void RecognitionModelWidget::onEditHotwords()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Hot Words"));
    dialog.resize(420, 320);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *hintRow = new QHBoxLayout;
    hintRow->setSpacing(8);
    auto *iconLabel = new QLabel(QStringLiteral("💡"), &dialog);
    iconLabel->setObjectName(QStringLiteral("hotwordsHintIcon"));
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setFixedSize(20, 20);
    auto *hintLabel = new QLabel(tr("One hot word per line."), &dialog);
    hintLabel->setObjectName(QStringLiteral("hotwordsHintLabel"));
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

} // namespace talkinput
