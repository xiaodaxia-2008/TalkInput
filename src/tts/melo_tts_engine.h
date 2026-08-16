#pragma once

#include "../tts_engine.h"

namespace talkinput
{

/// Offline MeloTTS backend running through the bundled sherpa-onnx TTS
/// runtime. The model is loaded lazily on first use and kept resident so
/// repeated requests stay fast.
class MeloTtsEngine final : public TtsEngine
{
public:
    MeloTtsEngine();
    ~MeloTtsEngine() override;

    TtsSynthesisResult synthesize(const QString &text, const QString &voice,
                                  double speed) override;

    QString name() const override { return QStringLiteral("melo"); }

    /// True if the model directory exists in the binary or app-data dirs.
    static bool isModelInstalled();

private:
    bool ensureLoaded(QString *error);
    QString modelDir() const;

    const void *m_tts = nullptr;
    int m_sampleRate = 0;
};

} // namespace talkinput
