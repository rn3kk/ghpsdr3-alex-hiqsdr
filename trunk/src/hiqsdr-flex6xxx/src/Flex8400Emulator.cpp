#include "Flex8400Emulator.h"

#include <QTimer>
#include <QtMath>

#include "FlexControlServer.h"
#include "HiqSdrBackend.h"
#include "RadioBackend.h"
#include "TestRadioBackend.h"
#include "VitaPacketBuilder.h"
#include "VitaUdpStreamer.h"

namespace {
constexpr int kAudioIntervalMs = 20;
constexpr int kAudioFramesPerPacket = 480;
constexpr int kSourceFrameRate = 25;

struct WaterfallCalibration
{
    int rate;
    double millisecondsPerRow;
};

constexpr WaterfallCalibration kWaterfallCalibration[] = {
    {1, 6000.0}, {8, 4000.9}, {50, 677.2}, {56, 473.2},
    {67, 223.0}, {69, 192.1}, {71, 163.9}, {75, 120.7},
    {77, 102.0}, {78, 90.5}, {79, 88.8}, {80, 81.2},
    {83, 64.2}, {90, 46.3}, {93, 42.0}, {100, 42.0}
};

int waterfallIntervalMs(int rate)
{
    if (rate <= kWaterfallCalibration[0].rate) {
        return qRound(kWaterfallCalibration[0].millisecondsPerRow);
    }

    constexpr int lastIndex = 15;
    for (int index = 1; index <= lastIndex; ++index) {
        const WaterfallCalibration& lower = kWaterfallCalibration[index - 1];
        const WaterfallCalibration& upper = kWaterfallCalibration[index];
        if (rate <= upper.rate) {
            const double fraction = static_cast<double>(rate - lower.rate)
                / static_cast<double>(upper.rate - lower.rate);
            const double logarithmicInterval = qLn(lower.millisecondsPerRow)
                + fraction * (qLn(upper.millisecondsPerRow)
                    - qLn(lower.millisecondsPerRow));
            return qRound(qExp(logarithmicInterval));
        }
    }

    return qRound(kWaterfallCalibration[lastIndex].millisecondsPerRow);
}
}

Flex8400Emulator::Flex8400Emulator(const QString& hiqSdrAddress, QObject* parent)
    : QObject(parent),
      m_radioBackend(hiqSdrAddress.isEmpty()
          ? static_cast<RadioBackend*>(new TestRadioBackend(this))
          : static_cast<RadioBackend*>(new HiqSdrBackend(hiqSdrAddress, this))),
      m_controlServer(new FlexControlServer(m_radioBackend, this)),
      m_udpStreamer(new VitaUdpStreamer(this)),
      m_frameProcessingTimer(new QTimer(this)),
      m_spectrumTimer(new QTimer(this)),
      m_waterfallTimer(new QTimer(this)),
      m_audioTimer(new QTimer(this))
{
    m_frameProcessingTimer->setInterval(1000 / kSourceFrameRate);
    m_spectrumTimer->setInterval(1000 / m_controlServer->spectrumFps());
    m_waterfallTimer->setInterval(waterfallIntervalMs(m_controlServer->waterfallRate()));
    m_audioTimer->setInterval(kAudioIntervalMs);
    m_packetBuilder.setNetworkMtu(m_controlServer->networkMtu());
    connect(m_frameProcessingTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onFrameProcessingTimeout);
    connect(m_spectrumTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onSpectrumTimeout);
    connect(m_audioTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onAudioTimeout);
    connect(m_waterfallTimer, &QTimer::timeout,
            this, &Flex8400Emulator::onWaterfallTimeout);
    connect(m_controlServer, &FlexControlServer::spectrumFpsChanged,
            this, &Flex8400Emulator::onSpectrumFpsChanged);
    connect(m_controlServer, &FlexControlServer::waterfallRateChanged,
            this, &Flex8400Emulator::onWaterfallRateChanged);
    connect(m_controlServer, &FlexControlServer::networkMtuChanged,
            this, &Flex8400Emulator::onNetworkMtuChanged);
}

bool Flex8400Emulator::start(const QHostAddress& address, quint16 port)
{
    if (!m_radioBackend->start()) {
        return false;
    }
    if (!m_controlServer->start(address, port)) {
        return false;
    }
    m_frameProcessingTimer->start();
    m_spectrumTimer->start();
    m_waterfallTimer->start();
    m_audioTimer->start();
    return true;
}

void Flex8400Emulator::onFrameProcessingTimeout()
{
    if (m_controlServer->isPanUpdatePending()) {
        return;
    }
    if (!m_radioBackend->hasSpectrumFrame()) {
        return;
    }

    // The source frame rate stays fixed so waterfall updates do not depend on FFT FPS.
    m_lastSpectrumFrame = m_radioBackend->createSpectrumFrame();
    m_hasSpectrumFrame = true;
}

void Flex8400Emulator::onSpectrumTimeout()
{
    const QList<UdpEndpoint> endpoints = m_controlServer->udpEndpoints();
    if (endpoints.isEmpty()) {
        return;
    }
    if (m_controlServer->isPanUpdatePending()) {
        return;
    }

    if (!m_hasSpectrumFrame) {
        return;
    }

    m_udpStreamer->send(m_packetBuilder.createSpectrumPackets(m_lastSpectrumFrame), endpoints);
    m_udpStreamer->send(
        m_packetBuilder.createMeterPacket(
            m_radioBackend->filterLevelDbm()),
        endpoints);
}

void Flex8400Emulator::onWaterfallTimeout()
{
    if (!m_hasSpectrumFrame || m_controlServer->isPanUpdatePending()) {
        return;
    }
    const QList<UdpEndpoint> endpoints = m_controlServer->udpEndpoints();
    if (!endpoints.isEmpty()) {
        SpectrumFrame waterfallFrame = m_lastSpectrumFrame;
        // AetherSDR uses this number to identify a completed waterfall row.
        waterfallFrame.index = m_waterfallFrameIndex++;
        m_udpStreamer->send(m_packetBuilder.createWaterfallPackets(waterfallFrame), endpoints);
    }
}

void Flex8400Emulator::onAudioTimeout()
{
    const QList<UdpEndpoint> endpoints = m_controlServer->udpEndpoints();
    if (endpoints.isEmpty()) {
        return;
    }

    const QVector<float> audio = m_radioBackend->createUsbAudio(kAudioFramesPerPacket);
    if (audio.size() != kAudioFramesPerPacket) {
        return;
    }
    m_udpStreamer->send(m_packetBuilder.createAudioPackets(audio), endpoints);
}

void Flex8400Emulator::onSpectrumFpsChanged(int framesPerSecond)
{
    m_spectrumTimer->setInterval(1000 / framesPerSecond);
}

void Flex8400Emulator::onWaterfallRateChanged(int rate)
{
    m_waterfallTimer->setInterval(waterfallIntervalMs(rate));
}

void Flex8400Emulator::onNetworkMtuChanged(int mtu)
{
    m_packetBuilder.setNetworkMtu(mtu);
}
