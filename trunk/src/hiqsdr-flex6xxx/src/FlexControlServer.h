#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>

#include "RadioTypes.h"

class QTimer;
class RadioBackend;

class FlexControlServer : public QObject
{
    Q_OBJECT

public:
    explicit FlexControlServer(RadioBackend* radioBackend, QObject* parent = nullptr);

    bool start(const QHostAddress& address, quint16 port);
    QList<UdpEndpoint> udpEndpoints() const;
    bool isPanUpdatePending() const;
    int spectrumFps() const;
    int waterfallRate() const;
    int networkMtu() const;

signals:
    void spectrumFpsChanged(int framesPerSecond);
    void waterfallRateChanged(int rate);
    void networkMtuChanged(int mtu);

private slots:
    void onNewConnection();
    void onClientReadyRead();
    void onClientDisconnected();
    void onDiscoveryTimeout();
    void onPanUpdateTimeout();
    void onSettingsSaveTimeout();

private:
    void processCommand(QTcpSocket* socket, const QString& line);
    bool processSliceTune(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processSliceSet(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processFilter(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processClientCommand(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processKeepaliveCommand(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processTransmitCommand(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processSubscription(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processDisplayCommand(QTcpSocket* socket, quint32 sequence, const QString& command);
    bool processStreamCommand(QTcpSocket* socket, quint32 sequence, const QString& command);
    void sendUnsupportedCommand(QTcpSocket* socket, quint32 sequence,
                                const QString& command) const;
    void sendLine(QTcpSocket* socket, const QString& line) const;
    void sendResponse(QTcpSocket* socket, quint32 sequence, quint32 code,
                      const QString& body = QString()) const;
    void sendInitialStatus(QTcpSocket* socket);
    void sendRadioStatus(QTcpSocket* socket) const;
    void sendSliceStatus(QTcpSocket* socket) const;
    void sendPanStatus(QTcpSocket* socket) const;
    void sendPanStatusToClients() const;
    void sendMeterStatus(QTcpSocket* socket) const;
    void sendTransmitStatus(QTcpSocket* socket) const;
    void sendDiscoveryPacket();
    void loadSettings();
    void loadDisplayProfile();
    void scheduleSettingsSave();
    void saveSettings() const;
    quint32 clientHandle(QTcpSocket* socket) const;

    RadioBackend* m_radioBackend;
    QTcpServer m_server;
    QUdpSocket m_discoverySocket;
    QTimer* m_discoveryTimer;
    QTimer* m_panUpdateTimer;
    QTimer* m_settingsSaveTimer;
    QMap<QTcpSocket*, QByteArray> m_readBuffers;
    QMap<QTcpSocket*, quint32> m_clientHandles;
    QMap<QTcpSocket*, quint16> m_clientUdpPorts;
    QHostAddress m_listenAddress;
    quint16 m_port{4992};
    quint32 m_nextHandle{0xF8400001};
    double m_pendingPanCenterFrequencyMhz{0.0};
    int m_pendingPanBandwidthHz{0};
    bool m_panUpdatePending{false};
    double m_panMinimumDbm{-140.0};
    double m_panMaximumDbm{-20.0};
    int m_panRfGain{0};
    int m_panSpectrumPoints{700};
    int m_spectrumFps{20};
    int m_waterfallRate{25};
    int m_networkMtu{1450};
    int m_waterfallBlackLevel{15};
    int m_waterfallColorGain{50};
    bool m_waterfallAutoBlack{true};
    bool m_txAssigned{true};
    bool m_transmitting{false};
};
