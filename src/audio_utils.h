#pragma once

#include <QAudioFormat>
#include <QByteArray>

namespace talkinput
{

void appendPcm16Sample(QByteArray &audioData, qint16 sample);
qint16 floatSampleToPcm16(float sample);
QByteArray convertAudioToPcm16(const QByteArray &audioData,
                               const QAudioFormat &format);

} // namespace talkinput
