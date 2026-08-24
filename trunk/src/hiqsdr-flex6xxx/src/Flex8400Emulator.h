#pragma once

#include <QHostAddress>
#include <QObject>

#include "VitaPacketBuilder.h"

class FlexControlServer;
class RadioBackend;
class VitaUdpStreamer;
class QTimer;

class Flex8400Emulator : public QObject
{
    Q_OBJECT

public:
    explicit Flex8400Emulator(const QString& hiqSdrAddress = QString(),
                              QObject* parent = nullptr);

    bool start(const QHostAddress& address, quint16 port);

private slots:
    void onFrameProcessingTimeout();
    void onSpectrumTimeout();
    void onWaterfallTimeout();
    void onAudioTimeout();
    void onSpectrumFpsChanged(int framesPerSecond);
    void onWaterfallRateChanged(int rate);
    void onNetworkMtuChanged(int mtu);

private:
    RadioBackend* m_radioBackend;
    FlexControlServer* m_controlServer;
    VitaPacketBuilder m_packetBuilder;
    VitaUdpStreamer* m_udpStreamer;
    QTimer* m_frameProcessingTimer;
    QTimer* m_spectrumTimer;
    QTimer* m_waterfallTimer;
    QTimer* m_audioTimer;
    SpectrumFrame m_lastSpectrumFrame;
    quint32 m_waterfallFrameIndex{0};
    bool m_hasSpectrumFrame{false};
};
