#pragma once

#include <QByteArray>
#include <QVector>

#include "RadioTypes.h"

class VitaPacketBuilder
{
public:
    QByteArray createSpectrumPacket(const SpectrumFrame& frame);
    QByteArray createWaterfallPacket(const SpectrumFrame& frame);
    QByteArray createAudioPacket(const QVector<float>& monoAudio);
    QByteArray createMeterPacket(float levelDbm);

private:
    quint8 m_spectrumSequence{0};
    quint8 m_audioSequence{0};
    quint8 m_meterSequence{0};
};
