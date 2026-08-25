#pragma once

#include <QByteArray>
#include <QString>

namespace zenny
{

/// Audio produced by a TtsEngine: 24 kHz, 16-bit, mono, little-endian PCM.
/// This is the format the API server hands to OpenAI-style clients.
struct TtsSynthesisResult
{
    QByteArray pcm24k;
    QString error;

    bool ok() const { return error.isEmpty(); }
};

/// Abstraction over the text-to-speech backends exposed through the
/// OpenAI-compatible /v1/audio/speech endpoint.
class TtsEngine
{
public:
    virtual ~TtsEngine() = default;

    /// Blocks until synthesis finishes. On success, returns 24 kHz int16 mono
    /// PCM. @p voice is the OpenAI-style voice requested by the client; an
    /// empty or unrecognized value falls back to the configured default.
    virtual TtsSynthesisResult synthesize(const QString &text,
                                          const QString &voice,
                                          double speed) = 0;

    virtual QString name() const = 0;
};

} // namespace zenny
