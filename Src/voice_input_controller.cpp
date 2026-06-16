#include "voice_input_controller.h"
#include "asr_service.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMediaDevices>
#include <QScreen>
#include <QTimer>
#include <QtEndian>

#include <spdlog/spdlog.h>
#include "qt_fmt.h"

#define NOMINMAX
#include <windows.h>

namespace {

// ── PCM16 conversion helpers (copied from main_window.cpp) ────────

void appendInt16(QByteArray &audioData, qint16 sample) {
  const qsizetype offset = audioData.size();
  audioData.resize(offset + static_cast<qsizetype>(sizeof(qint16)));
  qToLittleEndian<qint16>(
      sample, reinterpret_cast<uchar *>(audioData.data() + offset));
}

qint16 floatToInt16(float sample) {
  const float clamped = std::clamp(sample, -1.0F, 1.0F);
  return static_cast<qint16>(clamped * 32767.0F);
}

// ── Overlay window ─────────────────────────────────────────────

class OverlayWindow : public QWidget {
public:
  OverlayWindow()
      : QWidget(nullptr),
        m_animTimer(new QTimer(this)) {
    setWindowTitle(QStringLiteral("TalkInput"));
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedSize(220, 70);

    setStyleSheet(
        QStringLiteral("OverlayWindow { background: rgba(0,0,0,180); "
                       "border-radius: 12px; }"));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(16, 8, 16, 8);

    auto *micLabel = new QLabel(QStringLiteral("\xF0\x9F\x8E\x99"), this);
    micLabel->setStyleSheet(
        QStringLiteral("font-size: 24px; background: transparent;"));
    lay->addWidget(micLabel);

    auto *textLabel = new QLabel(
        QStringLiteral("<span style='color:#ff5050;font-size:16px;"
                       "font-weight:bold;'>REC</span>"),
        this);
    textLabel->setStyleSheet(QStringLiteral("background: transparent;"));
    lay->addWidget(textLabel);

    lay->addStretch();

    m_animTimer->setInterval(600);
    connect(m_animTimer, &QTimer::timeout, this, [this, micLabel]() {
      m_micVisible = !m_micVisible;
      micLabel->setVisible(m_micVisible);
    });
  }

  void startAnimation() {
    m_micVisible = true;
    positionOnActiveScreen();
    show();
    raise();

    HWND hwnd = reinterpret_cast<HWND>(winId());
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    m_animTimer->start();
  }

  void stopAnimation() {
    m_animTimer->stop();
    hide();
  }

private:
  void positionOnActiveScreen() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (auto *fg = GetForegroundWindow()) {
      HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
      MONITORINFO mi = {};
      mi.cbSize = sizeof(mi);
      if (GetMonitorInfo(mon, &mi)) {
        int w = mi.rcWork.right - mi.rcWork.left;
        int h = mi.rcWork.bottom - mi.rcWork.top;
        move((mi.rcWork.left + w / 2) - width() / 2,
             mi.rcWork.top + h - height() - 20);
        return;
      }
    }
    if (screen) {
      QRect wr = screen->availableGeometry();
      move(wr.left() + wr.width() / 2 - width() / 2,
           wr.top() + wr.height() - height() - 20);
    }
  }

  QTimer *m_animTimer;
  bool m_micVisible = true;
};

} // namespace

// ── VoiceInputController ─────────────────────────────────────────

namespace talkinput {

VoiceInputController::VoiceInputController(AsrService *asrService,
                                             RecognitionHistory *history,
                                             QObject *parent)
    : QObject(parent),
      m_asrService(asrService),
      m_history(history) {
  m_overlay = std::make_unique<OverlayWindow>();
  registerHotKey();

  connect(m_asrService, &AsrService::resultChanged, this,
          &VoiceInputController::onResult);
}

VoiceInputController::~VoiceInputController() {
  stopListening();
  unregisterHotKey();
}

bool VoiceInputController::nativeEventFilter(const QByteArray &eventType,
                                              void *message,
                                              qintptr *result) {
  Q_UNUSED(result);
  if (eventType == "windows_generic_MSG") {
    auto *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == m_hotKeyId) {
      if (m_isListening)
        stopListening();
      else
        startListening();
      return true;
    }
  }
  return false;
}

bool VoiceInputController::startListening() {
  spdlog::info("VoiceInputController: start listening");

  if (m_isListening) {
    spdlog::warn("Already listening");
    return false;
  }

  if (!m_asrService->isModelLoaded()) {
    spdlog::warn("ASR model not loaded");
    emit statusMessage(tr("Model not loaded yet. Please wait or select a model."));
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
      m_audioFormat.sampleFormat() == QAudioFormat::Unknown) {
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

  m_audioSource =
      std::make_unique<QAudioSource>(inputDevice, m_audioFormat);
  m_audioDevice = m_audioSource->start();
  if (!m_audioDevice) {
    spdlog::error("Failed to start microphone");
    m_audioSource.reset();
    emit statusMessage(tr("Failed to start microphone."));
    return false;
  }

  connect(m_audioDevice, &QIODevice::readyRead, this, [this]() {
    if (!m_audioDevice)
      return;
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

void VoiceInputController::stopListening() {
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
  hideOverlay();
  emit listeningChanged(false);
}

void VoiceInputController::onResult(const QString &text, bool isFinal) {
  m_lastResult = text;
  if (isFinal && !m_isListening && !text.trimmed().isEmpty()) {
    sendText(text.trimmed());
    m_history->addEntry(text.trimmed());
    spdlog::info("VoiceInputController injected and saved: {}", text);
  }
}

void VoiceInputController::sendText(const QString &text) {
  if (text.isEmpty())
    return;

  spdlog::info("Sending text to foreground app: {}", text);

  QVector<INPUT> inputs;
  inputs.reserve(text.size() * 2);

  for (const QChar ch : text) {
    if (ch.unicode() == 0)
      continue;

    INPUT keyDown = {};
    keyDown.type = INPUT_KEYBOARD;
    keyDown.ki.wVk = 0;
    keyDown.ki.wScan = ch.unicode();
    keyDown.ki.dwFlags = KEYEVENTF_UNICODE;

    INPUT keyUp = {};
    keyUp.type = INPUT_KEYBOARD;
    keyUp.ki.wVk = 0;
    keyUp.ki.wScan = ch.unicode();
    keyUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

    inputs.append(keyDown);
    inputs.append(keyUp);
  }

  SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
}

void VoiceInputController::showOverlay() {
  if (m_overlay)
    static_cast<OverlayWindow *>(m_overlay.get())->startAnimation();
}

void VoiceInputController::hideOverlay() {
  if (m_overlay)
    static_cast<OverlayWindow *>(m_overlay.get())->stopAnimation();
}

void VoiceInputController::registerHotKey() {
  m_hotKeyId = 1;
  if (!RegisterHotKey(nullptr, m_hotKeyId, MOD_NOREPEAT, VK_RMENU)) {
    spdlog::warn("Failed to register global hotkey (Right Alt)");
  } else {
    spdlog::info("Global hotkey registered: Right Alt");
  }
}

void VoiceInputController::unregisterHotKey() {
  if (m_hotKeyId != 0) {
    UnregisterHotKey(nullptr, m_hotKeyId);
    m_hotKeyId = 0;
  }
}

QByteArray VoiceInputController::convertToPcm16(
    const QByteArray &audioData, const QAudioFormat &format) {
  if (audioData.isEmpty())
    return {};

  if (format.sampleFormat() == QAudioFormat::Int16)
    return audioData;

  QByteArray pcm16;

  switch (format.sampleFormat()) {
  case QAudioFormat::UInt8:
    pcm16.reserve(audioData.size() * 2);
    for (const char byte : audioData) {
      const auto sample = static_cast<unsigned char>(byte);
      appendInt16(pcm16,
                  static_cast<qint16>((static_cast<int>(sample) - 128) << 8));
    }
    break;
  case QAudioFormat::Int32: {
    const int sampleCount = audioData.size() / static_cast<int>(sizeof(qint32));
    pcm16.reserve(sampleCount * 2);
    const auto *data = reinterpret_cast<const uchar *>(audioData.constData());
    for (int i = 0; i < sampleCount; ++i) {
      const qint32 sample =
          qFromLittleEndian<qint32>(data + i * sizeof(qint32));
      appendInt16(pcm16, static_cast<qint16>(sample >> 16));
    }
    break;
  }
  case QAudioFormat::Float: {
    const int sampleCount = audioData.size() / static_cast<int>(sizeof(float));
    pcm16.reserve(sampleCount * 2);
    for (int i = 0; i < sampleCount; ++i) {
      float sample = 0.0F;
      std::memcpy(&sample,
                  audioData.constData() + i * static_cast<int>(sizeof(float)),
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
