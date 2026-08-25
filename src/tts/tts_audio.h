#pragma once

#include <QByteArray>
#include <QtGlobal>

namespace zenny
{

/// Linear-interpolation resample of mono float samples in [-1, 1] to
/// 16-bit signed little-endian PCM at @p outputRate.
QByteArray resampleFloatToInt16(const float *samples, qsizetype n,
                                int inputRate, int outputRate);

/// Wraps 16-bit mono PCM in a WAV (RIFF) container.
QByteArray pcm16ToWav(const QByteArray &pcm16, int sampleRate);

QByteArray pcm16ToMp3(const QByteArray &pcm16, int sampleRate,
                      QString *error);

} // namespace zenny
