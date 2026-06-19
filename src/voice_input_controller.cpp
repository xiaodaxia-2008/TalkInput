#include "voice_input_controller.h"
#include "app_config.h"
#include "asr_service.h"
#include "llm_post_processor.h"
#include "logging.h"
#include "ocr_service.h"
#include "paste_text.h"
#include "scroll_text_display.h"
#include "utils.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QDateTime>
#include <QDir>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QtEndian>

#define NOMINMAX
#include <imm.h>
#include <vector>
#include <windows.h>
#pragma comment(lib, "imm32")

namespace
{

// ── PCM16 conversion helpers (copied from main_window.cpp) ────────

void appendInt16(QByteArray &audioData, qint16 sample)
{
    const qsizetype offset = audioData.size();
    audioData.resize(offset + static_cast<qsizetype>(sizeof(qint16)));
    qToLittleEndian<qint16>(
        sample, reinterpret_cast<uchar *>(audioData.data() + offset));
}

qint16 floatToInt16(float sample)
{
    const float clamped = std::clamp(sample, -1.0F, 1.0F);
    return static_cast<qint16>(clamped * 32767.0F);
}

void saveOcrDebugImage(const QImage &image)
{
    if (image.isNull()) {
        return;
    }

    const QString dir = QDir(talkinput::appDataDir()).filePath("ocr");
    QDir().mkpath(dir);
    const QString timestamp =
        QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss-zzz");
    const QString path =
        QDir(dir).filePath(QString("ocr-context-%1.png").arg(timestamp));
    const QString latestPath = QDir(dir).filePath("ocr-context-latest.png");

    if (image.save(path, "PNG")) {
        image.save(latestPath, "PNG");
        SPDLOG_DEBUG("OCR context debug screenshot saved: {}", path);
    }
    else {
        SPDLOG_WARN("OCR context debug screenshot save failed: {}", path);
    }
}

QScreen *screenByName(const QString &name)
{
    if (name.isEmpty()) {
        return nullptr;
    }

    for (QScreen *screen : QGuiApplication::screens()) {
        if (screen && screen->name().compare(name, Qt::CaseInsensitive) == 0) {
            return screen;
        }
    }
    return nullptr;
}

// ── Win32 acrylic blur helper ──────────────────────────────────

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

        HWND hwnd = reinterpret_cast<HWND>(winId());
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        enableAcrylic(hwnd);
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

VoiceInputController::VoiceInputController(AsrService *asrService,
                                           RecognitionHistory *history,
                                           QObject *parent)
    : QObject(parent), m_asrService(asrService), m_history(history)
{
    m_overlay = std::make_unique<OverlayWindow>();
    m_llmPostProcessor = new LlmPostProcessor(this);
    m_ocrService = createOcrService(this);
    registerHotKey();

    connect(m_asrService, &AsrService::resultChanged, this,
            &VoiceInputController::onResult);
}

VoiceInputController::~VoiceInputController()
{
    stopListening();
    unregisterHotKey();
}

bool VoiceInputController::nativeEventFilter(const QByteArray &eventType,
                                             void *message, qintptr *result)
{
    Q_UNUSED(result);
    if (eventType == "windows_generic_MSG") {
        auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_HOTKEY && msg->wParam == m_hotKeyId) {
            if (m_isListening) {
                stopListening();
            }
            else {
                startListening();
            }
            return true;
        }
    }
    return false;
}

bool VoiceInputController::startListening()
{
    SPDLOG_INFO("VoiceInputController: start listening");

    if (m_isListening) {
        SPDLOG_WARN("Already listening");
        return false;
    }

    if (!m_asrService->isModelLoaded()) {
        SPDLOG_WARN("ASR model not loaded");
        spdlog::get("statusbar")
            ->info("{}",
                   tr("Model not loaded yet. Please wait or select a model."));
        return false;
    }

    QMetaObject::invokeMethod(m_asrService, "startSession",
                              Qt::QueuedConnection);

    if (!m_asrService->acceptsExternalAudio()) {
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
        const QByteArray pcm16 = convertToPcm16(audioData, m_audioFormat);
        QMetaObject::invokeMethod(
            m_asrService,
            [this, pcm16]() {
                m_asrService->feedAudio(pcm16, m_audioFormat.sampleRate(),
                                        m_audioFormat.channelCount());
            },
            Qt::QueuedConnection);
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

    // Finish the session on the worker thread
    QMetaObject::invokeMethod(m_asrService, "finishSession",
                              Qt::QueuedConnection);

    m_isListening = false;
    m_pendingResult = true;
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
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        return;
    }

    auto submitToLlm = [this, finalText](const QString &ocrContext) {
        if (m_llmPostProcessor->isEnabled()) {
            SPDLOG_DEBUG("OCR context sent to LLM: {}", ocrContext.trimmed());
            spdlog::get("statusbar")
                ->info("{}", tr("Post-processing recognition result..."));
        }
        const QString hotwords = []() -> QString {
            const nlohmann::json hw =
                talkinput::appConfigValue("/settings/asr/hotwords");
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
            return lines.join(QLatin1Char('\n'));
        }();
        m_llmPostProcessor->postProcess(
            finalText, ocrContext, hotwords, this,
            [this, finalText](const QString &processedText) {
                SPDLOG_DEBUG(
                    "Voice input final text after LLM: input='{}' output='{}'",
                    finalText, processedText);
                injectFinalText(processedText.trimmed());
            });
    };

    const bool llmEnabled = m_llmPostProcessor->isEnabled();
    const bool ocrEnabled =
        appConfigBool("/settings/ocr/useFocusedInputContext", false);
    const bool ocrServiceAvailable =
        m_ocrService && m_ocrService->isAvailable();
    SPDLOG_DEBUG("OCR context flow: llmEnabled={} ocrEnabled={} "
                 "ocrServiceAvailable={}",
                 llmEnabled, ocrEnabled, ocrServiceAvailable);

    if (!llmEnabled) {
        SPDLOG_DEBUG("OCR context skipped: LLM post-processing is disabled");
        submitToLlm({});
        return;
    }
    if (!ocrEnabled) {
        SPDLOG_DEBUG("OCR context skipped: OCR focused context is disabled");
        submitToLlm({});
        return;
    }
    if (!ocrServiceAvailable) {
        SPDLOG_DEBUG("OCR context skipped: OCR service is unavailable");
        submitToLlm({});
        return;
    }

    const QImage image = captureFocusedContextImage();
    if (image.isNull()) {
        SPDLOG_DEBUG("OCR context skipped: no focused screenshot");
        submitToLlm({});
        return;
    }

    SPDLOG_DEBUG("OCR context screenshot captured: {}x{}", image.width(),
                 image.height());
    spdlog::get("statusbar")
        ->info("{}", tr("Reading focused input context..."));
    m_ocrService->recognizeText(
        image, this, [submitToLlm](const QString &contextText) mutable {
            const QString result = contextText.trimmed();
            SPDLOG_DEBUG("OCR context result received: {}", result);
            submitToLlm(result);
        });
}

QImage VoiceInputController::captureFocusedContextImage() const
{
    if (m_ocrService) {
        const QImage focusedWindowImage =
            m_ocrService->captureFocusedTextInputImage();
        if (!focusedWindowImage.isNull()) {
            SPDLOG_DEBUG("OCR context focused window screenshot captured: "
                         "{}x{}",
                         focusedWindowImage.width(),
                         focusedWindowImage.height());
            saveOcrDebugImage(focusedWindowImage);
            return focusedWindowImage;
        }
        SPDLOG_DEBUG("OCR context focused window screenshot failed; falling "
                     "back to full screen");
    }

    const QString screenName =
        m_ocrService ? m_ocrService->focusedTextInputScreenName() : QString();
    QScreen *screen = screenByName(screenName);
    if (screen) {
        SPDLOG_DEBUG("OCR context matched focused screen '{}'", screen->name());
    }
    if (!screen) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        SPDLOG_DEBUG("OCR context screenshot skipped: no screen");
        return {};
    }

    const QPixmap pixmap = screen->grabWindow(0);
    const QImage image = pixmap.toImage();
    SPDLOG_DEBUG("OCR context using full-screen fallback on screen '{}': "
                 "{}x{} dpr={}",
                 screen->name(), pixmap.width(), pixmap.height(),
                 pixmap.devicePixelRatio());
    saveOcrDebugImage(image);
    return image;
}

void VoiceInputController::injectFinalText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    sendText(text);
    if (m_history) {
        m_history->addEntry(text);
    }
    emit finalTextCommitted(text);
    SPDLOG_INFO("VoiceInputController injected and saved: {}", text);
}

// ── Clipboard paste helper ────────────────────────────────────
// 先填写剪切板、等待异步传播完成、再发 Ctrl+V。
// 粘贴后不清除剪切板，文本留给用户备用。
static bool tryClipboardPaste(const QString &text)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return false;
    }

    if (!OpenClipboard(nullptr)) {
        return false;
    }

    EmptyClipboard();

    const int len = text.length();
    HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(len + 1) *
                                                     sizeof(wchar_t));
    if (!hGlobal) {
        CloseClipboard();
        return false;
    }

    auto *dst = static_cast<wchar_t *>(GlobalLock(hGlobal));
    text.toWCharArray(dst);
    dst[len] = L'\0';
    GlobalUnlock(hGlobal);

    if (!SetClipboardData(CF_UNICODETEXT, hGlobal)) {
        GlobalFree(hGlobal);
        CloseClipboard();
        return false;
    }
    CloseClipboard();

    // 剪切板更新是异步的 — 等待传播完成再发 Ctrl+V
    // 轮询 GetClipboardSequenceNumber() 直到变化，再加 20 ms 保险
    const DWORD seqBefore = GetClipboardSequenceNumber();
    for (int tries = 0; tries < 50; ++tries) {
        if (GetClipboardSequenceNumber() != seqBefore) {
            break;
        }
        Sleep(10);
    }
    Sleep(20);

    // Ctrl+V
    INPUT ctrlV[4] = {};
    ctrlV[0].type = INPUT_KEYBOARD;
    ctrlV[0].ki.wVk = VK_CONTROL;
    ctrlV[1].type = INPUT_KEYBOARD;
    ctrlV[1].ki.wVk = 'V';
    ctrlV[2].type = INPUT_KEYBOARD;
    ctrlV[2].ki.wVk = 'V';
    ctrlV[2].ki.dwFlags = KEYEVENTF_KEYUP;
    ctrlV[3].type = INPUT_KEYBOARD;
    ctrlV[3].ki.wVk = VK_CONTROL;
    ctrlV[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(4, ctrlV, sizeof(INPUT));

    // 不还原剪切板 — 文本留给用户
    return true;
}

// ── SendInput 模式：逐字 KEYEVENTF_UNICODE + ASCII 延时 ────
static void sendViaSendInput(const QString &text)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    DWORD tid = GetWindowThreadProcessId(hwnd, nullptr);
    DWORD ourTid = GetCurrentThreadId();
    const BOOL attached =
        (tid != ourTid) ? AttachThreadInput(ourTid, tid, TRUE) : FALSE;

    HIMC himc = ImmGetContext(hwnd);
    const BOOL wasOpen = himc ? ImmGetOpenStatus(himc) : FALSE;
    if (himc) {
        ImmSetOpenStatus(himc, FALSE);
    }

    for (const QChar ch : text) {
        const ushort code = ch.unicode();
        if (code == 0) {
            continue;
        }

        INPUT pair[2] = {};
        pair[0].type = INPUT_KEYBOARD;
        pair[0].ki.wVk = 0;
        pair[0].ki.wScan = code;
        pair[0].ki.dwFlags = KEYEVENTF_UNICODE;

        pair[1].type = INPUT_KEYBOARD;
        pair[1].ki.wVk = 0;
        pair[1].ki.wScan = code;
        pair[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, pair, sizeof(INPUT));

        if (code < 0x80) {
            Sleep(15);
        }
    }

    if (himc) {
        ImmSetOpenStatus(himc, wasOpen);
        ImmReleaseContext(hwnd, himc);
    }

    if (attached) {
        AttachThreadInput(ourTid, tid, FALSE);
    }
}

// ── 终端检测 ──────────────────────────────────────────────────
static bool isTerminalWindow(HWND hwnd)
{
    wchar_t cls[256];
    if (!GetClassNameW(hwnd, cls, 256)) {
        return false;
    }
    return wcscmp(cls, L"ConsoleWindowClass") == 0 ||
           wcscmp(cls, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0;
}

// ── VoiceInputController::sendText ────────────────────────────
void VoiceInputController::sendText(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    SPDLOG_INFO("Sending text to foreground app: {}", text);

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    if (isTerminalWindow(hwnd)) {
        // 终端窗口：剪切板 + Ctrl+V（绕过英文丢字问题）
        SPDLOG_DEBUG("Terminal window, using clipboard paste");
        if (!tryClipboardPaste(text)) {
            sendViaSendInput(text);
        }
    }
    else {
        // 非终端窗口：直接字符发送（兼容性更好）
        sendViaSendInput(text);
    }
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
    m_hotKeyId = 1;
    if (!RegisterHotKey(nullptr, m_hotKeyId,
                        MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'L'))
    {
        SPDLOG_WARN("Failed to register global hotkey (Ctrl+Alt+L)");
    }
    else {
        SPDLOG_INFO("Global hotkey registered: Ctrl+Alt+L");
    }
}

void VoiceInputController::unregisterHotKey()
{
    if (m_hotKeyId != 0) {
        UnregisterHotKey(nullptr, m_hotKeyId);
        m_hotKeyId = 0;
    }
}

QByteArray VoiceInputController::convertToPcm16(const QByteArray &audioData,
                                                const QAudioFormat &format)
{
    if (audioData.isEmpty()) {
        return {};
    }

    if (format.sampleFormat() == QAudioFormat::Int16) {
        return audioData;
    }

    QByteArray pcm16;

    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8:
        pcm16.reserve(audioData.size() * 2);
        for (const char byte : audioData) {
            const auto sample = static_cast<unsigned char>(byte);
            appendInt16(pcm16, static_cast<qint16>(
                                   (static_cast<int>(sample) - 128) << 8));
        }
        break;
    case QAudioFormat::Int32: {
        const int sampleCount =
            audioData.size() / static_cast<int>(sizeof(qint32));
        pcm16.reserve(sampleCount * 2);
        const auto *data =
            reinterpret_cast<const uchar *>(audioData.constData());
        for (int i = 0; i < sampleCount; ++i) {
            const qint32 sample =
                qFromLittleEndian<qint32>(data + i * sizeof(qint32));
            appendInt16(pcm16, static_cast<qint16>(sample >> 16));
        }
        break;
    }
    case QAudioFormat::Float: {
        const int sampleCount =
            audioData.size() / static_cast<int>(sizeof(float));
        pcm16.reserve(sampleCount * 2);
        for (int i = 0; i < sampleCount; ++i) {
            float sample = 0.0F;
            std::memcpy(&sample,
                        audioData.constData() +
                            i * static_cast<int>(sizeof(float)),
                        sizeof(float));
            appendInt16(pcm16, floatToInt16(sample));
        }
        break;
    }
    default:
        break;
    }

    return pcm16;
}

} // namespace talkinput
