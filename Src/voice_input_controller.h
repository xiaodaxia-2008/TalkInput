#pragma once

#include "recognition_history.h"
#include <QAbstractNativeEventFilter>
#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <memory>

class QAudioSource;
class QIODevice;
class QTimer;
class QWidget;

namespace talkinput {

class AsrService;

class VoiceInputController final : public QObject,
                                    public QAbstractNativeEventFilter {
  Q_OBJECT

public:
  explicit VoiceInputController(AsrService *asrService,
                                 RecognitionHistory *history,
                                 QObject *parent = nullptr);
  ~VoiceInputController() override;

  bool nativeEventFilter(const QByteArray &eventType, void *message,
                         qintptr *result) override;

  bool isListening() const { return m_isListening; }

signals:
  void listeningChanged(bool listening);
  void statusMessage(const QString &msg);

public slots:
  bool startListening();
  void stopListening();

private:
  void registerHotKey();
  void unregisterHotKey();
  void onResult(const QString &text, bool isFinal);
  void sendText(const QString &text);
  void showOverlay();
  void hideOverlay();
  QByteArray convertToPcm16(const QByteArray &data,
                            const QAudioFormat &format);

  AsrService *m_asrService;
  RecognitionHistory *m_history;

  std::unique_ptr<QAudioSource> m_audioSource;
  QIODevice *m_audioDevice = nullptr;
  QAudioFormat m_audioFormat;
  int m_hotKeyId = 0;
  bool m_isListening = false;

  std::unique_ptr<QWidget> m_overlay;
  QString m_lastResult;
};

} // namespace talkinput
