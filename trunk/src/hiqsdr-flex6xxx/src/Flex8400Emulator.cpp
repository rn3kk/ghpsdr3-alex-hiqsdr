#include "Flex8400Emulator.h"

#include <QTimer>

#include "FlexControlServer.h"
#include "RadioBackend.h"
#include "TestRadioBackend.h"
#include "VitaPacketBuilder.h"
#include "VitaUdpStreamer.h"

namespace {
constexpr int kSpectrumIntervalMs = 40;
constexpr int kAudioIntervalMs = 20;
constexpr int kAudioFramesPerPacket = 480;
}

Flex8400Emulator::Flex8400Emulator(QObject* parent)
    : QObject(parent),
      m_radioBackend(new TestRadioBackend(this)),
      m_controlServer(new FlexControlServer(m_radioBackend, this)),
      m_udpStreamer(new VitaUdpStreamer(this)),
      m_spectrumTimer(new QTimer(this)),
      m_audioTimer(new QTimer(this))
{
    m_spectrumTimer->setInterval(kSpectrumIntervalMs);
    m_audioTimer->setInterval(kAudioIntervalMs);
    connect(m_spectrumTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onSpectrumTimeout);
    connect(m_audioTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onAudioTimeout);
}

bool Flex8400Emulator::start(const QHostAddress& address, quint16 port)
{
    if (!m_controlServer->start(address, port)) {
        return false;
    }
    m_spectrumTimer->start();
    m_audioTimer->start();
    return true;
}

void Flex8400Emulator::onSpectrumTimeout()
{
    const QList<UdpEndpoint> endpoints = m_controlServer->udpEndpoints();
    if (endpoints.isEmpty()) {
        return;
    }

    const SpectrumFrame frame = m_radioBackend->createSpectrumFrame();
    m_udpStreamer->send(m_packetBuilder.createSpectrumPacket(frame), endpoints);
    m_udpStreamer->send(m_packetBuilder.createWaterfallPacket(frame), endpoints);
    m_udpStreamer->send(
        m_packetBuilder.createMeterPacket(
            m_radioBackend->filterLevelDbm()),
        endpoints);
}

void Flex8400Emulator::onAudioTimeout()
{
    const QList<UdpEndpoint> endpoints = m_controlServer->udpEndpoints();
    if (endpoints.isEmpty()) {
        return;
    }

    const QVector<float> audio = m_radioBackend->createUsbAudio(kAudioFramesPerPacket);
    m_udpStreamer->send(m_packetBuilder.createAudioPacket(audio), endpoints);
}
