#include "VitaPacketBuilder.h"

#include <QtEndian>

#include <cstring>

namespace {
constexpr int kSpectrumPixels = 700;
constexpr float kSpectrumMinDbm = -140.0f;
constexpr float kSpectrumMaxDbm = -20.0f;
constexpr int kWaterfallMinIntensity = 60;
constexpr int kWaterfallMaxIntensity = 120;
constexpr quint32 kPanStreamId = 0x40000000;
constexpr quint32 kWaterfallStreamId = 0x42000000;
constexpr quint32 kAudioStreamId = 0x43000000;
constexpr quint32 kFlexOui = 0x00001C2D;
constexpr quint32 kFftClass = 0x534C8003;
constexpr quint32 kWaterfallClass = 0x534C8004;
constexpr quint32 kNarrowAudioClass = 0x534C03E3;
constexpr quint32 kMeterClass = 0x534C8002;
constexpr quint32 kMeterStreamId = 0x46000000;
constexpr int kIpv4AndUdpHeaderBytes = 28;

quint32 packetWord(quint8 sequence, quint16 words)
{
    return 0x30000000u | (static_cast<quint32>(sequence) << 16) | words;
}

void writeHeader(uchar* data, quint32 word0, quint32 streamId, quint32 classCode)
{
    qToBigEndian(word0, data);
    qToBigEndian(streamId, data + 4);
    qToBigEndian(kFlexOui, data + 8);
    qToBigEndian(classCode, data + 12);
    qToBigEndian<quint32>(0, data + 16);
    qToBigEndian<quint32>(0, data + 20);
    qToBigEndian<quint32>(0, data + 24);
}
}

void VitaPacketBuilder::setNetworkMtu(int mtu)
{
    m_networkMtu = mtu;
}

QList<QByteArray> VitaPacketBuilder::createSpectrumPackets(const SpectrumFrame& frame)
{
    constexpr int headerBytes = 28;
    constexpr int subheaderBytes = 12;
    const int binsPerPacket = qMax(1,
        (m_networkMtu - kIpv4AndUdpHeaderBytes - headerBytes - subheaderBytes) / 2);
    QList<QByteArray> packets;
    for (int firstBin = 0; firstBin < frame.pixels.size(); firstBin += binsPerPacket) {
        const int binCount = qMin(binsPerPacket, frame.pixels.size() - firstBin);
        const int packetBytes = headerBytes + subheaderBytes + binCount * 2;
        QByteArray packet(packetBytes, Qt::Uninitialized);
        uchar* data = reinterpret_cast<uchar*>(packet.data());
        writeHeader(data, packetWord(m_spectrumSequence, packetBytes / 4), kPanStreamId, kFftClass);
        qToBigEndian<quint16>(firstBin, data + headerBytes);
        qToBigEndian<quint16>(binCount, data + headerBytes + 2);
        qToBigEndian<quint16>(sizeof(quint16), data + headerBytes + 4);
        qToBigEndian<quint16>(frame.pixels.size(), data + headerBytes + 6);
        qToBigEndian(frame.index, data + headerBytes + 8);
        for (int index = 0; index < binCount; ++index) {
            qToBigEndian(frame.pixels.at(firstBin + index),
                          data + headerBytes + subheaderBytes + index * 2);
        }
        packets.append(packet);
        m_spectrumSequence = (m_spectrumSequence + 1) & 0x0F;
    }
    return packets;
}

QList<QByteArray> VitaPacketBuilder::createWaterfallPackets(const SpectrumFrame& frame)
{
    constexpr int headerBytes = 28;
    constexpr int subheaderBytes = 36;
    const int binsPerPacket = qMax(1,
        (m_networkMtu - kIpv4AndUdpHeaderBytes - headerBytes - subheaderBytes) / 2);
    const qint64 lowFrequency = (frame.centerFrequencyHz - frame.bandwidthHz / 2) * 1048576LL;
    // A Flex waterfall tile stores the width of one bin as Hz x 2^20.
    const qint64 binBandwidth = qRound64(
        static_cast<double>(frame.bandwidthHz) / frame.pixels.size() * 1048576.0);
    QList<QByteArray> packets;
    for (int firstBin = 0; firstBin < frame.pixels.size(); firstBin += binsPerPacket) {
        const int binCount = qMin(binsPerPacket, frame.pixels.size() - firstBin);
        const int packetBytes = headerBytes + subheaderBytes + binCount * 2;
        QByteArray packet(packetBytes, Qt::Uninitialized);
        uchar* data = reinterpret_cast<uchar*>(packet.data());
        writeHeader(data, packetWord(m_waterfallSequence, packetBytes / 4),
                    kWaterfallStreamId, kWaterfallClass);
        qToBigEndian(lowFrequency, data + headerBytes);
        qToBigEndian(binBandwidth, data + headerBytes + 8);
        qToBigEndian<quint32>(40, data + headerBytes + 16);
        qToBigEndian<quint16>(binCount, data + headerBytes + 20);
        qToBigEndian<quint16>(1, data + headerBytes + 22);
        qToBigEndian(frame.index, data + headerBytes + 24);
        qToBigEndian<quint32>(96 * 128, data + headerBytes + 28);
        qToBigEndian<quint16>(frame.pixels.size(), data + headerBytes + 32);
        qToBigEndian<quint16>(firstBin, data + headerBytes + 34);
        for (int index = 0; index < binCount; ++index) {
            const float dbm = frame.maximumDbm
                - static_cast<float>(frame.pixels.at(firstBin + index))
                    * (frame.maximumDbm - frame.minimumDbm) / (kSpectrumPixels - 1);
            const int intensity = qBound(kWaterfallMinIntensity,
                kWaterfallMinIntensity + qRound((dbm - frame.minimumDbm)
                    / (frame.maximumDbm - frame.minimumDbm)
                    * (kWaterfallMaxIntensity - kWaterfallMinIntensity)),
                kWaterfallMaxIntensity);
            qToBigEndian<quint16>(static_cast<quint16>(intensity * 128),
                                  data + headerBytes + subheaderBytes + index * 2);
        }
        packets.append(packet);
        m_waterfallSequence = (m_waterfallSequence + 1) & 0x0F;
    }
    return packets;
}

QByteArray VitaPacketBuilder::createAudioPacket(const QVector<float>& monoAudio)
{
    constexpr int headerBytes = 28;
    const int packetBytes = headerBytes + monoAudio.size() * 2 * sizeof(float);
    QByteArray packet(packetBytes, Qt::Uninitialized);
    uchar* data = reinterpret_cast<uchar*>(packet.data());
    writeHeader(data, packetWord(m_audioSequence, packetBytes / 4),
                kAudioStreamId, kNarrowAudioClass);

    for (int index = 0; index < monoAudio.size(); ++index) {
        quint32 bits = 0;
        std::memcpy(&bits, &monoAudio.at(index), sizeof(bits));
        qToBigEndian(bits, data + headerBytes + index * 2 * sizeof(float));
        qToBigEndian(bits, data + headerBytes + (index * 2 + 1) * sizeof(float));
    }

    m_audioSequence = (m_audioSequence + 1) & 0x0F;
    return packet;
}

QByteArray VitaPacketBuilder::createMeterPacket(float levelDbm)
{
    constexpr int headerBytes = 28;
    constexpr int payloadBytes = 4;
    constexpr int packetBytes = headerBytes + payloadBytes;
    QByteArray packet(packetBytes, Qt::Uninitialized);
    uchar* data = reinterpret_cast<uchar*>(packet.data());
    writeHeader(data, packetWord(m_meterSequence, packetBytes / 4),
                kMeterStreamId, kMeterClass);
    qToBigEndian<quint16>(1, data + headerBytes);
    qToBigEndian<qint16>(static_cast<qint16>(qRound(levelDbm * 128.0f)),
                          data + headerBytes + 2);
    m_meterSequence = (m_meterSequence + 1) & 0x0F;
    return packet;
}
