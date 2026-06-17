#include "voice_input_controller.h"
#include "asr_service.h"
#include "logging.h"
#include "paste_text.h"
#include "scroll_text_display.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
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
        setWindowTitle(QStringLiteral("TalkInput"));
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                       Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedHeight(72);

        setStyleSheet(
            QStringLiteral("OverlayWindow { background: rgba(16,16,18,180); "
                           "border: 1px solid rgba(255,255,255,36); "
                           "border-radius: 14px; }"));

        auto *lay = new QHBoxLayout(this);
        lay->setContentsMargins(14, 6, 14, 6);
        lay->setSpacing(8);

        auto *micLabel = new QLabel(QStringLiteral("\xF0\x9F\x8E\x99"), this);
        micLabel->setStyleSheet(
            QStringLiteral("font-size: 24px; background: transparent;"));
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
    spdlog::info("VoiceInputController: start listening");

    if (m_isListening) {
        spdlog::warn("Already listening");
        return false;
    }

    if (!m_asrService->isModelLoaded()) {
        spdlog::warn("ASR model not loaded");
        emit statusMessage(
            tr("Model not loaded yet. Please wait or select a model."));
        return false;
    }

    const QAudioDevice inputDevice = QMediaDevices::defaultAudioInput();
    if (inputDevice.isNull()) {
        spdlog::error("No audio input device");
        emit statusMessage(tr("No microphone available."));
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
        spdlog::error("Audio format not supported");
        emit statusMessage(tr("Microphone format not supported."));
        return false;
    }

    // Start a new session on the ASR worker thread
    QMetaObject::invokeMethod(m_asrService, "startSession",
                              Qt::QueuedConnection);

    m_audioSource = std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
    m_audioDevice = m_audioSource->start();
    if (!m_audioDevice) {
        spdlog::error("Failed to start microphone");
        m_audioSource.reset();
        emit statusMessage(tr("Failed to start microphone."));
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
    spdlog::info("Voice input started");
    return true;
}

void VoiceInputController::stopListening()
{
    spdlog::info("VoiceInputController: stop listening");

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
            sendText(text.trimmed());
            m_history->addEntry(text.trimmed());
            spdlog::info("VoiceInputController injected and saved: {}", text);
        }
    }
    else if (m_isListening && text != m_lastResult) {
        m_lastResult = text;
        if (m_overlay) {
            static_cast<OverlayWindow *>(m_overlay.get())->setPreviewText(text);
        }
    }
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

    spdlog::info("Sending text to foreground app: {}", text);

    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        return;
    }

    if (isTerminalWindow(hwnd)) {
        // 终端窗口：剪切板 + Ctrl+V（绕过英文丢字问题）
        spdlog::debug("Terminal window, using clipboard paste");
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
        spdlog::warn("Failed to register global hotkey (Ctrl+Alt+L)");
    }
    else {
        spdlog::info("Global hotkey registered: Ctrl+Alt+L");
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
