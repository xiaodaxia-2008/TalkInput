#include "tts_settings_widget.h"
#include "app_config.h"
#include "archive_utils.h"
#include "logging.h"
#include "model_download.h"
#include "tts/edge_tts_engine.h"
#include "tts/melo_tts_engine.h"
#include "tts/tts_audio.h"
#include "tts_engine.h"
#include "utils.h"
#include "ui_tts_settings_widget.h"

#include <QAudioOutput>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMediaPlayer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextEdit>
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
    m_ui = std::make_unique<Ui::TtsSettingsWidget>();
    m_ui->setupUi(this);
    m_ui->providerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->providerCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_ui->providerCombo->addItem(tr("Edge (Online)"), QStringLiteral("edge"));
    m_ui->providerCombo->addItem(tr("MeloTTS (Offline)"), QStringLiteral("melo"));
    m_ui->voiceCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ui->voiceCombo->setInsertPolicy(QComboBox::NoInsert);
    m_ui->voiceCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    const char *voices[] = {
        "zh-CN-XiaoxiaoNeural", "zh-CN-XiaoyiNeural",  "zh-CN-YunxiNeural",
        "zh-CN-YunjianNeural",  "zh-CN-YunyangNeural", "en-US-AriaNeural",
        "en-US-GuyNeural",      "en-US-JennyNeural",   "ja-JP-NanamiNeural",
        "ko-KR-SunHiNeural",
    };
    for (const char *voice : voices) {
        m_ui->voiceCombo->addItem(QString::fromLatin1(voice));
    }
    m_ui->modelStatusLabel->setSizePolicy(QSizePolicy::Expanding,
                                          QSizePolicy::Preferred);
    m_ui->modelStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    const int iconSize = fontMetrics().height();
    setButtonIcon(m_ui->browserButton, ":/resources/icons/globe.svg", iconSize);
    m_ui->browserButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->importButton, ":/resources/icons/import.svg", iconSize);
    m_ui->importButton->setProperty("buttonRole", "icon");
    setButtonIcon(m_ui->downloadButton, ":/resources/icons/download.svg", iconSize);
    m_ui->downloadButton->setProperty("buttonRole", "icon");

    m_audioOutput = new QAudioOutput(this);
    m_mediaPlayer = new QMediaPlayer(this);
    m_ui->playPreviewButton->setEnabled(false);
    m_ui->savePreviewButton->setEnabled(false);
    m_mediaPlayer->setAudioOutput(m_audioOutput);

    auto saveVoice = [this]() {
        appConfig().settings.ttsEdgeVoice =
            m_ui->voiceCombo->currentText().trimmed().toStdString();
        markConfigDirty();
        STATUSBAR_INFO("{}", tr("TTS voice saved"));
    };
    connect(m_ui->voiceCombo->lineEdit(), &QLineEdit::editingFinished, this,
            saveVoice);
    connect(m_ui->voiceCombo, &QComboBox::activated, this,
            [saveVoice](int) { saveVoice(); });

    connect(m_ui->providerCombo, &QComboBox::currentIndexChanged, this,
            &TtsSettingsWidget::onTtsProviderChanged);

    connect(m_ui->downloadButton, &QPushButton::clicked, this,
            [this]() { auto task = downloadTtsModel(); });
    connect(m_ui->browserButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::onOpenTtsModelUrl);
    connect(m_ui->importButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::onImportTtsModel);
    connect(m_ui->previewButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::synthesizePreview);
    connect(m_ui->playPreviewButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::playPreview);
    connect(m_ui->savePreviewButton, &QPushButton::clicked, this,
            &TtsSettingsWidget::savePreview);
}

void TtsSettingsWidget::retranslate()
{
    m_ui->providerFormLabel->setText(tr("Provider"));
    m_ui->voiceFormLabel->setText(tr("Voice"));
    m_ui->modelFormLabel->setText(tr("Model"));
    m_ui->previewFormLabel->setText(tr("Preview"));
    m_ui->previewButton->setText(tr("Convert to speech"));
    m_ui->playPreviewButton->setText(tr("Play"));
    m_ui->savePreviewButton->setText(tr("Save MP3"));
    m_ui->previewEdit->setPlaceholderText(tr("Enter text to synthesize"));
    m_ui->voiceCombo->lineEdit()->setPlaceholderText(
        tr("Voice name, e.g. zh-CN-XiaoxiaoNeural"));
}

void TtsSettingsWidget::synthesizePreview()
{
    const QString text = m_ui->previewEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        STATUSBAR_INFO("{}", tr("Enter text to synthesize."));
        return;
    }

    std::unique_ptr<TtsEngine> engine;
    if (m_ui->providerCombo->currentData().toString() == QStringLiteral("melo")) {
        engine = std::make_unique<MeloTtsEngine>();
    }
    else {
        engine = std::make_unique<EdgeTtsEngine>();
    }

    m_ui->previewButton->setEnabled(false);
    const TtsSynthesisResult result = engine->synthesize(
        text, m_ui->voiceCombo->currentText(), 1.0);
    m_ui->previewButton->setEnabled(true);
    if (!result.ok()) {
        STATUSBAR_INFO("{}", tr("Speech conversion failed: %1").arg(result.error));
        return;
    }

    m_previewPcm = result.pcm24k;
    m_ui->playPreviewButton->setEnabled(true);
    m_ui->savePreviewButton->setEnabled(true);

    m_previewFile = std::make_unique<QTemporaryFile>();
    m_previewFile->setAutoRemove(true);
    if (!m_previewFile->open()) {
        STATUSBAR_INFO("{}", tr("Could not prepare audio playback."));
        return;
    }
    const QByteArray wav = pcm16ToWav(result.pcm24k, 24000);
    if (m_previewFile->write(wav) != wav.size()) {
        STATUSBAR_INFO("{}", tr("Could not prepare audio playback."));
        return;
    }
    m_previewFile->flush();
    m_previewFile->close();
    m_mediaPlayer->setSource(QUrl::fromLocalFile(m_previewFile->fileName()));
    m_mediaPlayer->play();
}

void TtsSettingsWidget::playPreview()
{
    if (m_previewFile) {
        m_mediaPlayer->setSource(QUrl::fromLocalFile(m_previewFile->fileName()));
        m_mediaPlayer->play();
    }
}

void TtsSettingsWidget::savePreview()
{
    if (m_previewPcm.isEmpty()) {
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save speech"), QString(), tr("MP3 audio (*.mp3)"));
    if (path.isEmpty()) {
        return;
    }

    QString error;
    const QByteArray mp3 = pcm16ToMp3(m_previewPcm, 24000, &error);
    if (mp3.isEmpty()) {
        STATUSBAR_INFO("{}", tr("Failed to save MP3: %1").arg(error));
        return;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(mp3) != mp3.size()) {
        STATUSBAR_INFO("{}", tr("Failed to save MP3 file."));
        return;
    }
    STATUSBAR_INFO("{}", tr("MP3 saved."));
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
    const int providerIndex = m_ui->providerCombo->findData(provider);
    {
        const QSignalBlocker blocker(m_ui->providerCombo);
        if (providerIndex >= 0) {
            m_ui->providerCombo->setCurrentIndex(providerIndex);
        }
    }

    {
        const QSignalBlocker blocker(m_ui->voiceCombo);
        m_ui->voiceCombo->setEditText(
            QString::fromStdString(appConfig().settings.ttsEdgeVoice));
    }

    updateTtsWidgetStates();
    refreshTtsModelStatus();
}

void TtsSettingsWidget::updateTtsWidgetStates()
{
    const bool isEdge =
        m_ui->providerCombo->currentData().toString() == QStringLiteral("edge");

    m_ui->voiceFormLabel->setEnabled(isEdge);
    m_ui->voiceCombo->setEnabled(isEdge);
    m_ui->modelFormLabel->setVisible(!isEdge);
    m_ui->modelStatusLabel->setVisible(!isEdge);
    m_ui->browserButton->setVisible(!isEdge);
    m_ui->importButton->setVisible(!isEdge);
    m_ui->downloadButton->setVisible(!isEdge);
}

void TtsSettingsWidget::refreshTtsModelStatus()
{
    const bool installed = MeloTtsEngine::isModelInstalled();
    m_ui->modelStatusLabel->setText(installed ? tr("MeloTTS model installed")
                                          : tr("MeloTTS model not installed"));
    m_ui->downloadButton->setEnabled(!installed);
}

void TtsSettingsWidget::onTtsProviderChanged(int /*index*/)
{
    appConfig().settings.ttsProvider =
        m_ui->providerCombo->currentData().toString().toStdString();
    markConfigDirty();
    updateTtsWidgetStates();
    refreshTtsModelStatus();
    STATUSBAR_INFO(
        "{}", tr("TTS provider saved: %1").arg(m_ui->providerCombo->currentText()));
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
