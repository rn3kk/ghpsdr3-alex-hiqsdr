#pragma once

#include <QByteArray>
#include <QList>
#include <QVector>

#include "RadioTypes.h"

class VitaPacketBuilder
{
public:
    void setNetworkMtu(int mtu);
    QList<QByteArray> createSpectrumPackets(const SpectrumFrame& frame);
    QList<QByteArray> createWaterfallPackets(const SpectrumFrame& frame);
    QByteArray createAudioPacket(const QVector<float>& monoAudio);
    QByteArray createMeterPacket(float levelDbm);

private:
    int m_networkMtu{1450};
    quint8 m_spectrumSequence{0};
    quint8 m_waterfallSequence{0};
    quint8 m_audioSequence{0};
    quint8 m_meterSequence{0};
};
