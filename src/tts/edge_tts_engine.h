#pragma once

#include "../tts_engine.h"

namespace talkinput
{

/// Microsoft Edge online TTS backend (edge-tts compatible protocol over a
/// WebSocket). Requests 24 kHz MP3 audio and decodes it to 24 kHz PCM.
class EdgeTtsEngine final : public TtsEngine
{
public:
    TtsSynthesisResult synthesize(const QString &text, const QString &voice,
                                  double speed) override;

    QString name() const override
    {
        return QStringLiteral("edge");
    }
};

} // namespace talkinput
