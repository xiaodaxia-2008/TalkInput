#include "tts_settings_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_download.h"
#include "tts/melo_tts_engine.h"
#include "utils.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

namespace talkinput
{

TtsSettingsWidget::TtsSettingsWidget(QWidget *parent) : QWidget(parent)
{
    buildUi();
    retranslate();
    refreshFromConfig();
}

TtsSettingsWidget::~TtsSettingsWidget() = default;

void TtsSettingsWidget::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setObjectName(QStringLiteral("settingsScroll"));

    auto *content = new QWidget(scroll);
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(12);

    m_group = new QGroupBox(content);
    auto *grid = new QGridLayout(m_group);
    grid->setContentsMargins(16, 20, 16, 14);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(10);

    m_providerFormLabel = new QLabel(m_group);
    grid->addWidget(m_providerFormLabel, 0, 0);

    m_providerCombo = new QComboBox(m_group);
    m_providerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_providerCombo->addItem(tr("Edge (Online)"), QStringLiteral("edge"));
    m_providerCombo->addItem(tr("MeloTTS (Offline)"), QStringLiteral("melo"));
    grid->addWidget(m_providerCombo, 0, 1);

    m_voiceFormLabel = new QLabel(m_group);
    grid->addWidget(m_voiceFormLabel, 1, 0);

    m_voiceCombo = new QComboBox(m_group);
    m_voiceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_voiceCombo->setEditable(true);
    m_voiceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_voiceCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const char *voices[] = {
        "zh-CN-XiaoxiaoNeural", "zh-CN-XiaoyiNeural",  "zh-CN-YunxiNeural",
        "zh-CN-YunjianNeural",  "zh-CN-YunyangNeural", "en-US-AriaNeural",
        "en-US-GuyNeural",      "en-US-JennyNeural",   "ja-JP-NanamiNeural",
        "ko-KR-SunHiNeural",
    };
    for (const char *voice : voices) {
        m_voiceCombo->addItem(QString::fromLatin1(voice));
    }
    grid->addWidget(m_voiceCombo, 1, 1);

    m_modelFormLabel = new QLabel(m_group);
    grid->addWidget(m_modelFormLabel, 2, 0);

    auto *modelRow = new QHBoxLayout;
    modelRow->setSpacing(8);

    m_modelStatusLabel = new QLabel(m_group);
    m_modelStatusLabel->setSizePolicy(QSizePolicy::Expanding,
                                      QSizePolicy::Preferred);
    m_modelStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    modelRow->addWidget(m_modelStatusLabel, 1);

    m_browserButton = new QPushButton(m_group);
    m_browserButton->setToolTip(tr("Open download page in browser"));
    m_browserButton->setFlat(true);
    modelRow->addWidget(m_browserButton);

    m_importButton = new QPushButton(m_group);
    m_importButton->setToolTip(tr("Import downloaded model archive"));
    m_importButton->setFlat(true);
    modelRow->addWidget(m_importButton);

    m_downloadButton = new QPushButton(m_group);
    m_downloadButton->setToolTip(tr("Download MeloTTS model"));
    m_downloadButton->setFlat(true);
    modelRow->addWidget(m_downloadButton);

    grid->addLayout(modelRow, 2, 1);

    contentLayout->addWidget(m_group);
    contentLayout->addStretch();

    scroll->setWidget(content);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scroll);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_browserButton, ":/resources/icons/globe.svg", iconSize);
    m_browserButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_importButton, ":/resources/icons/import.svg", iconSize);
    m_importButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_downloadButton, ":/resources/icons/download.svg", iconSize);
    m_downloadButton->setProperty("buttonRole", "icon");

    auto saveVoice = [this]() {
        appConfig().settings.ttsEdgeVoice =
            m_voiceCombo->currentText().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("TTS voice saved"));
    };
    connect(m_voiceCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveVoice);
    connect(m_voiceCombo, &QComboBox::activated, this,
            [saveVoice](int) { saveVoice(); });

    connect(m_providerCombo, &QComboBox::currentIndexChanged, this,
            &TtsSettingsWidget::onTtsProviderChanged);

    connect(m_downloadButton, &QPushButton::clicked, this,
            [this]() { auto task = downloadTtsModel(); });
    connect(m_browserButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::onOpenTtsModelUrl);
    connect(m_importButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::onImportTtsModel);
}

void TtsSettingsWidget::retranslate()
{
    m_group->setTitle(tr("TTS Service"));
    m_providerFormLabel->setText(tr("Provider"));
    m_voiceFormLabel->setText(tr("Voice"));
    m_modelFormLabel->setText(tr("Model"));
    m_voiceCombo->lineEdit()->setPlaceholderText(
        tr("Voice name, e.g. zh-CN-XiaoxiaoNeural"));
}

void TtsSettingsWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
}

void TtsSettingsWidget::refreshFromConfig()
{
    const QString provider =
        QString::fromStdString(appConfig().settings.ttsProvider);
    const int providerIndex = m_providerCombo->findData(provider);
    {
        const QSignalBlocker blocker(m_providerCombo);
        if (providerIndex >= 0) {
            m_providerCombo->setCurrentIndex(providerIndex);
        }
    }

    {
        const QSignalBlocker blocker(m_voiceCombo);
        m_voiceCombo->setEditText(
            QString::fromStdString(appConfig().settings.ttsEdgeVoice));
    }

    updateTtsWidgetStates();
    refreshTtsModelStatus();
}

void TtsSettingsWidget::updateTtsWidgetStates()
{
    const bool isEdge =
        m_providerCombo->currentData().toString() == QStringLiteral("edge");

    m_voiceFormLabel->setEnabled(isEdge);
    m_voiceCombo->setEnabled(isEdge);
    m_modelFormLabel->setVisible(!isEdge);
    m_modelStatusLabel->setVisible(!isEdge);
    m_browserButton->setVisible(!isEdge);
    m_importButton->setVisible(!isEdge);
    m_downloadButton->setVisible(!isEdge);
}

void TtsSettingsWidget::refreshTtsModelStatus()
{
    const bool installed = MeloTtsEngine::isModelInstalled();
    m_modelStatusLabel->setText(installed ? tr("MeloTTS model installed")
                                          : tr("MeloTTS model not installed"));
    m_downloadButton->setEnabled(!installed);
}

void TtsSettingsWidget::onTtsProviderChanged(int /*index*/)
{
    appConfig().settings.ttsProvider =
        m_providerCombo->currentData().toString().toStdString();
    markConfigDirty();
    updateTtsWidgetStates();
    refreshTtsModelStatus();
    STATUSBAR_INFO(
        "{}", tr("TTS provider saved: %1").arg(m_providerCombo->currentText()));
}

QCoro::Task<void> TtsSettingsWidget::downloadTtsModel()
{
    if (MeloTtsEngine::isModelInstalled()) {
        STATUSBAR_INFO("{}", tr("MeloTTS model is already installed."));
        co_return;
    }

    const QPointer<TtsSettingsWidget> guard(this);
    auto result = co_await downloadModelArchive(
        QStringLiteral("MeloTTS"),
        QString::fromStdString(appConfig().settings.ttsMeloModelUrl),
        QStringLiteral("TTS"));
    if (!guard) {
        co_return;
    }
    if (!result.ok) {
        STATUSBAR_INFO("{}",
                       tr("TTS model download failed: %1").arg(result.error));
        co_return;
    }

    refreshTtsModelStatus();
    STATUSBAR_INFO("{}", tr("MeloTTS model installed."));
}

void TtsSettingsWidget::onOpenTtsModelUrl()
{
    const std::string &url = appConfig().settings.ttsMeloModelUrl;
    if (url.empty()) {
        STATUSBAR_INFO("{}", tr("No download URL for this model."));
        return;
    }
    QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
}

void TtsSettingsWidget::onImportTtsModel()
{
    const QString expectedName =
        QFileInfo(
            QUrl(QString::fromStdString(appConfig().settings.ttsMeloModelUrl))
                .path())
            .fileName();
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

    STATUSBAR_INFO(
        "{}", tr("Extracting TTS model: %1").arg(QStringLiteral("MeloTTS")));
    auto result = extractArchive(destPath, modelRoot.absolutePath());
    QFile::remove(destPath);
    if (!result) {
        STATUSBAR_INFO(
            "{}", tr("TTS model extraction failed: %1").arg(result.error()));
        return;
    }

    refreshTtsModelStatus();
    STATUSBAR_INFO("{}",
                   tr("TTS model imported: %1").arg(QStringLiteral("MeloTTS")));
}

} // namespace talkinput
