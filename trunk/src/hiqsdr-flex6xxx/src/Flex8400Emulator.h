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
    void onSpectrumTimeout();
    void onAudioTimeout();

private:
    RadioBackend* m_radioBackend;
    FlexControlServer* m_controlServer;
    VitaPacketBuilder m_packetBuilder;
    VitaUdpStreamer* m_udpStreamer;
    QTimer* m_spectrumTimer;
    QTimer* m_audioTimer;
};
