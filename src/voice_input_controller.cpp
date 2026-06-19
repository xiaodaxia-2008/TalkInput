#include "voice_input_controller.h"
#include "asr_config.h"
#include "audio_utils.h"
#include "logging.h"
#include "paste_text.h"
#include "scroll_text_display.h"
#include "voice_text_processor.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QCursor>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHotkey>
#include <QLabel>
#include <QMediaDevices>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

namespace
{

#ifdef Q_OS_WIN
static void enableAcrylic(HWND hwnd)
{
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (!hUser) {
        return;
    }

    using SWCA = BOOL(WINAPI *)(HWND, void *);
    auto func = reinterpret_cast<SWCA>(
        GetProcAddress(hUser, "SetWindowCompositionAttribute"));
    if (!func) {
        return;
    }

    struct AccentPolicy
    {
        DWORD state;  // ACCENT_STATE
        DWORD flags;  // AccentFlags
        DWORD color;  // GradientColor (0xAARRGGBB)
        DWORD animId; // AnimationId
    };

    struct WinCompAttrData
    {
        DWORD attr; // WCA_ACCENT_POLICY = 19
        const void *data;
        DWORD dataSize;
    };

    // ACCENT_ENABLE_ACRYLIC (4) with dark tint
    AccentPolicy accent = {4, 0, 0xC0101012, 0};
    WinCompAttrData wcad = {19, &accent, sizeof(accent)};
    func(hwnd, &wcad);
}

void applyNativeOverlayEffects(QWidget *widget)
{
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    enableAcrylic(hwnd);
}
#else
void applyNativeOverlayEffects(QWidget *)
{
}
#endif

// ── Overlay window with marquee ────────────────────────────────

class OverlayWindow : public QWidget
{
public:
    OverlayWindow() : QWidget(nullptr)
    {
        setObjectName("voiceOverlay");
        setWindowTitle(QStringLiteral("TalkInput"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(72);

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(14, 6, 14, 6);
        lay->setSpacing(8);

        auto *micLabel = new QLabel(QStringLiteral("\xF0\x9F\x8E\x99"), this);
        micLabel->setObjectName("voiceOverlayMicLabel");
        lay->addWidget(micLabel);

        auto *effect = new QGraphicsOpacityEffect(micLabel);
        micLabel->setGraphicsEffect(effect);
        m_blinkAnim = new QPropertyAnimation(effect, "opacity", this);
        m_blinkAnim->setDuration(1200);
        m_blinkAnim->setStartValue(1.0);
        m_blinkAnim->setEndValue(0.15);
        m_blinkAnim->setLoopCount(-1);
        m_blinkAnim->setEasingCurve(QEasingCurve::InOutSine);

        m_scrollText = new ScrollTextDisplay(this);
        lay->addWidget(m_scrollText, 1);

        setMinimumWidth(320);
    }

    void startAnimation()
    {
        m_blinkAnim->start();
        m_scrollText->setText(QString());
        show();
        raise();
        positionOnActiveScreen();

        applyNativeOverlayEffects(this);
    }

    void stopAnimation()
    {
        m_blinkAnim->stop();
        static_cast<QGraphicsOpacityEffect *>(m_blinkAnim->targetObject())
            ->setOpacity(1.0);
        hide();
    }

    void setPreviewText(const QString &text)
    {
        m_scrollText->setText(text);
    }

private:
    void positionOnActiveScreen()
    {
        QPoint cursorPos = QCursor::pos();
        QScreen *screen = QGuiApplication::screenAt(cursorPos);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            return;
        }
        QRect wr = screen->availableGeometry();
        int x = wr.left() + wr.width() / 2 - width() / 2;
        if (x < wr.left()) {
            x = wr.left() + 8;
        }
        move(x, wr.bottom() - height() - 30);
    }

    QPropertyAnimation *m_blinkAnim = nullptr;
    ScrollTextDisplay *m_scrollText = nullptr;
};

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput
{

static VoiceInputController *s_instance = nullptr;

VoiceInputController *VoiceInputController::instance()
{
    return s_instance;
}

VoiceInputController::VoiceInputController(QObject *parent) : QObject(parent)
{
    s_instance = this;
    m_overlay = std::make_unique<OverlayWindow>();
    m_textProcessor = new VoiceTextProcessor(this);
    registerHotKey();
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    unregisterHotKey();
    s_instance = nullptr;
}

bool VoiceInputController::startListening()
{
    SPDLOG_INFO("VoiceInputController: start listening");

    if (m_isListening) {
        SPDLOG_WARN("Already listening");
        return false;
    }

    if (!m_recognizer) {
        SPDLOG_WARN("ASR model not loaded");
        spdlog::get("statusbar")
            ->info("{}",
                   tr("Model not loaded yet. Please wait or select a model."));
        return false;
    }

    m_recognizer->resetStream();

    if (!m_recognizer->acceptsExternalAudio()) {
        m_isListening = true;
        m_lastResult.clear();
        showOverlay();
        emit listeningChanged(true);
        SPDLOG_INFO("Voice input started with recognizer-owned audio source");
        return true;
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        SPDLOG_ERROR("No audio input device");
        spdlog::get("statusbar")->info("{}", tr("No microphone available"));
        return false;
    }

    m_audioFormat = inputDevice.preferredFormat();
    if (!m_audioFormat.isValid() ||
        m_audioFormat.sampleFormat() == QAudioFormat::Unknown)
    {
        m_audioFormat = QAudioFormat();
        m_audioFormat.setSampleRate(48000);
        m_audioFormat.setChannelCount(1);
        m_audioFormat.setSampleFormat(QAudioFormat::Int16);
    }

    if (!inputDevice.isFormatSupported(m_audioFormat)) {
        SPDLOG_ERROR("Audio format not supported");
        spdlog::get("statusbar")
            ->info("{}", tr("Microphone format not supported."));
        return false;
    }

    m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        SPDLOG_ERROR("Failed to start microphone");
        m_audioSource.reset();
        spdlog::get("statusbar")->info("{}", tr("Failed to start microphone"));
        return false;
    }

    connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
        if (!m_audioDevice) {
            return;
        }
        const QByteArray audioData = m_audioDevice->readAll();
        const QByteArray pcm16 = convertAudioToPcm16(audioData, m_audioFormat);
        if (m_recognizer) {
            m_recognizer->acceptPcm16(pcm16, m_audioFormat.sampleRate(),
                                      m_audioFormat.channelCount());
        }
    });

    m_isListening = true;
    m_lastResult.clear();
    showOverlay();
    emit listeningChanged(true);
    SPDLOG_INFO("Voice input started");
    return true;
}

void VoiceInputController::stopListening()
{
    SPDLOG_INFO("VoiceInputController: stop listening");

    if (m_audioSource) {
        m_audioSource->stop();
    }
    m_audioDevice = nullptr;
    m_audioSource.reset();

    if (m_recognizer && m_recognizer->isRunning()) {
        m_pendingResult = true;
        m_recognizer->finish();
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
    }

    m_isListening = false;
    hideOverlay();
    emit listeningChanged(false);
}

void VoiceInputController::onResult(const QString &text, bool isFinal)
{
    if (isFinal) {
        m_lastResult = text;
        const bool shouldSend = m_pendingResult || !m_isListening;
        m_pendingResult = false;
        if (shouldSend && !text.trimmed().isEmpty()) {
            postProcessFinalText(text.trimmed());
        }
    }
    else if (m_isListening && text != m_lastResult) {
        m_lastResult = text;
        if (m_overlay) {
            static_cast<OverlayWindow *>(m_overlay.get())->setPreviewText(text);
        }
    }
}

void VoiceInputController::postProcessFinalText(const QString &text)
{
    m_textProcessor->processFinalText(
        text, this, [this](const QString &processedText) {
            injectFinalText(processedText.trimmed());
        });
}

void VoiceInputController::injectFinalText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    sendText(text);
    emit finalTextCommitted(text);
    SPDLOG_INFO("VoiceInputController injected and saved: {}", text);
}

// ── VoiceInputController::sendText ────────────────────────────
void VoiceInputController::sendText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    SPDLOG_INFO("Sending text to foreground app: {}", text);
    pasteTextToActiveWindow(text, true, false);
}

void VoiceInputController::showOverlay()
{
    if (m_overlay) {
        static_cast<OverlayWindow *>(m_overlay.get())->startAnimation();
    }
}

void VoiceInputController::hideOverlay()
{
    if (m_overlay) {
        static_cast<OverlayWindow *>(m_overlay.get())->stopAnimation();
    }
}

void VoiceInputController::registerHotKey()
{
    if (!QHotkey::isPlatformSupported()) {
        SPDLOG_WARN("Global hotkeys are not supported on this platform");
        return;
    }

    m_hotkey =
        new QHotkey(QKeySequence(QStringLiteral("Ctrl+Alt+L")), true, this);
    connect(m_hotkey, &QHotkey::activated, this, [this]() {
        if (m_isListening) {
            stopListening();
        }
        else {
            startListening();
        }
    });

    if (!m_hotkey->isRegistered()) {
        SPDLOG_WARN("Failed to register global hotkey: Ctrl+Alt+L");
    }
    else {
        SPDLOG_INFO("Global hotkey registered: Ctrl+Alt+L");
    }
}

void VoiceInputController::unregisterHotKey()
{
    if (m_hotkey) {
        m_hotkey->setRegistered(false);
        delete m_hotkey;
        m_hotkey = nullptr;
    }
}

// ── SpeechRecognizer lifecycle ──────────────────────────────────

void VoiceInputController::loadModel(const nlohmann::json &preset)
{
    unloadModel();

    const QString modelDir = asrModelDir(preset);

    auto recognizer = SpeechRecognizer::createFromConfig(
        preset, modelDir, currentHotwordsConfig(), this);
    if (!recognizer) {
        SPDLOG_ERROR("VoiceInputController: model load failed: {}",
                     recognizer.error());
        emit modelLoadResult(false, recognizer.error());
        return;
    }

    connect(recognizer->get(), &SpeechRecognizer::resultChanged, this,
            &VoiceInputController::onResult);
    m_recognizer = std::move(*recognizer);

    // Persist providerId
    setCurrentAsrProviderId(jsonString(preset, "id"));

    emit modelLoadResult(true, {});
}

void VoiceInputController::unloadModel()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->stop();
    }
    m_recognizer.reset();
}

void VoiceInputController::startSession()
{
    if (m_recognizer) {
        m_recognizer->resetStream();
    }
}

void VoiceInputController::feedAudio(const QByteArray &pcm16, int sampleRate,
                                     int channels)
{
    if (m_recognizer) {
        m_recognizer->acceptPcm16(pcm16, sampleRate, channels);
    }
}

void VoiceInputController::finishSession()
{
    if (m_recognizer && m_recognizer->isRunning()) {
        m_recognizer->finish();
        if (m_recognizer->acceptsExternalAudio()) {
            m_recognizer->resetStream();
        }
    }
}

bool VoiceInputController::acceptsExternalAudio() const
{
    return !m_recognizer || m_recognizer->acceptsExternalAudio();
}

} // namespace talkinput
