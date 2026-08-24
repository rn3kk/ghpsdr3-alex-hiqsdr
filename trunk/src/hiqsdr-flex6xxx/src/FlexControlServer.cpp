#include "FlexControlServer.h"

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QTimer>
#include <QtMath>

#include "RadioBackend.h"

namespace {
const QString kModel = QStringLiteral("FLEX-8400");
const QString kSerial = QStringLiteral("HIQ84000001");
const QString kVersion = QStringLiteral("4.2.20.0");
const QString kRadioName = QStringLiteral("HiQSDR Flex Gateway");
constexpr quint32 kInvalidArgumentError = 0x50000002;
constexpr quint32 kUnsupportedCommandError = 0x50000015;
constexpr int kPanUpdateDelayMs = 150;
constexpr int kSettingsSaveDelayMs = 750;
constexpr int kMaximumSpectrumFps = 25;
constexpr int kMinimumWaterfallRate = 1;
constexpr int kMaximumWaterfallRate = 100;
constexpr int kMinimumNetworkMtu = 576;
constexpr int kMaximumNetworkMtu = 9000;
}

FlexControlServer::FlexControlServer(RadioBackend* radioBackend, QObject* parent)
    : QObject(parent),
      m_radioBackend(radioBackend),
      m_discoveryTimer(new QTimer(this)),
      m_panUpdateTimer(new QTimer(this)),
      m_settingsSaveTimer(new QTimer(this))
{
    m_discoveryTimer->setInterval(1000);
    m_panUpdateTimer->setSingleShot(true);
    m_settingsSaveTimer->setSingleShot(true);
    connect(&m_server, &QTcpServer::newConnection,
            this, &FlexControlServer::onNewConnection);
    connect(m_discoveryTimer, &QTimer::timeout,
            this, &FlexControlServer::onDiscoveryTimeout);
    connect(m_panUpdateTimer, &QTimer::timeout,
            this, &FlexControlServer::onPanUpdateTimeout);
    connect(m_settingsSaveTimer, &QTimer::timeout,
            this, &FlexControlServer::onSettingsSaveTimeout);
    loadSettings();
    // Keep IQ processing independent from the spectrum display slider.
    m_radioBackend->setSpectrumFrameRate(kMaximumSpectrumFps);
    if (!m_radioBackend->setSpectrumDbmRange(
            static_cast<float>(m_panMinimumDbm), static_cast<float>(m_panMaximumDbm))) {
        qWarning() << "Cannot apply saved display range";
    }
    scheduleSettingsSave();
}

bool FlexControlServer::start(const QHostAddress& address, quint16 port)
{
    m_listenAddress = address;
    m_port = port;
    if (!m_server.listen(address, port)) {
        qCritical() << "Cannot listen on" << address.toString() << port
                    << m_server.errorString();
        return false;
    }

    m_discoveryTimer->start();
    sendDiscoveryPacket();
    qInfo() << "FLEX-8400 control server listens on" << address.toString() << port;
    return true;
}

QList<UdpEndpoint> FlexControlServer::udpEndpoints() const
{
    QList<UdpEndpoint> endpoints;
    for (auto it = m_clientUdpPorts.cbegin(); it != m_clientUdpPorts.cend(); ++it) {
        QTcpSocket* socket = it.key();
        if (socket && socket->state() == QAbstractSocket::ConnectedState) {
            endpoints.append({socket->peerAddress(), it.value()});
        }
    }
    return endpoints;
}

bool FlexControlServer::isPanUpdatePending() const
{
    return m_panUpdatePending;
}

int FlexControlServer::spectrumFps() const
{
    return m_spectrumFps;
}

int FlexControlServer::waterfallRate() const
{
    return m_waterfallRate;
}

int FlexControlServer::networkMtu() const
{
    return m_networkMtu;
}

void FlexControlServer::onNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket* socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }
        const quint32 handle = m_nextHandle++;
        m_clientHandles.insert(socket, handle);
        connect(socket, &QTcpSocket::readyRead,
                this, &FlexControlServer::onClientReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &FlexControlServer::onClientDisconnected);
        sendLine(socket, QStringLiteral("V%1").arg(kVersion));
        sendLine(socket, QStringLiteral("H%1").arg(handle, 0, 16).toUpper());
        qInfo() << "Flex client connected:" << socket->peerAddress().toString()
                << "handle:" << QString::number(handle, 16).toUpper();
    }
}

void FlexControlServer::onClientReadyRead()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    QByteArray& buffer = m_readBuffers[socket];
    buffer.append(socket->readAll());
    int newlinePosition = -1;
    while ((newlinePosition = buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(buffer.left(newlinePosition)).trimmed();
        buffer.remove(0, newlinePosition + 1);
        if (!line.isEmpty()) {
            processCommand(socket, line);
        }
    }
}

void FlexControlServer::onClientDisconnected()
{
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) {
        return;
    }
    qWarning() << "Flex client disconnected:" << socket->peerAddress().toString()
               << "reason:" << socket->errorString();
    m_readBuffers.remove(socket);
    m_clientHandles.remove(socket);
    m_clientUdpPorts.remove(socket);
    socket->deleteLater();
}

void FlexControlServer::onDiscoveryTimeout()
{
    sendDiscoveryPacket();
}

void FlexControlServer::onPanUpdateTimeout()
{
    if (!m_panUpdatePending) {
        return;
    }

    m_panUpdatePending = false;
    if (!m_radioBackend->setPanBandwidthHz(m_pendingPanBandwidthHz)
        || !m_radioBackend->setPanCenterFrequencyMhz(m_pendingPanCenterFrequencyMhz)) {
        qWarning() << "Cannot apply requested pan settings";
        return;
    }

    loadDisplayProfile();
    scheduleSettingsSave();
    sendPanStatusToClients();
}

void FlexControlServer::onSettingsSaveTimeout()
{
    saveSettings();
}

void FlexControlServer::processCommand(QTcpSocket* socket, const QString& line)
{
    if (!line.startsWith(QLatin1Char('C'))) {
        return;
    }
    const int separator = line.indexOf(QLatin1Char('|'));
    if (separator < 2) {
        return;
    }
    bool sequenceIsValid = false;
    const quint32 sequence = line.mid(1, separator - 1).toUInt(&sequenceIsValid);
    if (!sequenceIsValid) {
        return;
    }

    const QString command = line.mid(separator + 1).trimmed();
    qInfo().noquote() << "Flex command:" << command;

    if (command.startsWith(QStringLiteral("client udpport "))) {
        bool portIsValid = false;
        const quint16 udpPort = command.mid(15).trimmed().toUShort(&portIsValid);
        if (!portIsValid || udpPort == 0) {
            sendResponse(socket, sequence, kInvalidArgumentError,
                         QStringLiteral("Invalid UDP port"));
            return;
        }
        m_clientUdpPorts.insert(socket, udpPort);
        sendResponse(socket, sequence, 0);
        return;
    }

    if (command == QStringLiteral("info")) {
        sendResponse(socket, sequence, 0,
                     QStringLiteral("callsign=HIQSDR,name=HiQSDR Flex Gateway,region=ITU,"
                                    "model=FLEX-8400,chassis_serial=HIQ84000001,"
                                    "software_ver=4.2.20.0,ip=127.0.0.1"));
        return;
    }
    if (command == QStringLiteral("slice list")) {
        sendResponse(socket, sequence, 0, QStringLiteral("0"));
        return;
    }
    if (command == QStringLiteral("mic list")) {
        sendResponse(socket, sequence, 0, QStringLiteral("MIC"));
        return;
    }
    if (command.startsWith(QStringLiteral("client gui"))) {
        sendInitialStatus(socket);
        sendResponse(socket, sequence, 0);
        return;
    }
    if (processSubscription(socket, sequence, command)
        || processSliceTune(socket, sequence, command)
        || processSliceSet(socket, sequence, command)
        || processFilter(socket, sequence, command)
        || processClientCommand(socket, sequence, command)
        || processKeepaliveCommand(socket, sequence, command)
        || processTransmitCommand(socket, sequence, command)
        || processDisplayCommand(socket, sequence, command)
        || processStreamCommand(socket, sequence, command)) {
        return;
    }
    sendUnsupportedCommand(socket, sequence, command);
}

bool FlexControlServer::processSliceTune(QTcpSocket* socket, quint32 sequence,
                                         const QString& command)
{
    if (!command.startsWith(QStringLiteral("slice tune "))) {
        return false;
    }
    const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool frequencyIsValid = false;
    if (parts.size() < 4 || parts.at(2) != QStringLiteral("0")) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid slice"));
        return true;
    }
    const double frequencyMhz = parts.at(3).toDouble(&frequencyIsValid);
    if (!frequencyIsValid || !m_radioBackend->setSliceFrequencyMhz(frequencyMhz)) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid frequency"));
        return true;
    }
    scheduleSettingsSave();
    sendSliceStatus(socket);
    sendResponse(socket, sequence, 0);
    return true;
}

bool FlexControlServer::processSliceSet(QTcpSocket* socket, quint32 sequence,
                                        const QString& command)
{
    if (!command.startsWith(QStringLiteral("slice set 0 "))) {
        return false;
    }
    const QString parameter = command.mid(QStringLiteral("slice set 0 ").size()).trimmed();
    const int separator = parameter.indexOf(QLatin1Char('='));
    if (separator <= 0) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid slice setting"));
        return true;
    }
    const QString name = parameter.left(separator);
    const QString value = parameter.mid(separator + 1);
    bool valueIsValid = false;
    bool updated = false;
    if (name == QStringLiteral("RF_frequency")) {
        updated = m_radioBackend->setSliceFrequencyMhz(value.toDouble(&valueIsValid));
        updated = updated && valueIsValid;
    } else if (name == QStringLiteral("mode")) {
        updated = m_radioBackend->setSliceMode(value.toUpper());
    } else if (name == QStringLiteral("agc_mode")) {
        updated = m_radioBackend->setAgcMode(value.toUpper());
    } else if (name == QStringLiteral("agc_threshold")) {
        updated = m_radioBackend->setAgcThreshold(value.toInt(&valueIsValid));
        updated = updated && valueIsValid;
    } else if (name == QStringLiteral("rxant")) {
        updated = m_radioBackend->setAntenna(value);
    } else if (name == QStringLiteral("preamp")) {
        const int enabled = value.toInt(&valueIsValid);
        updated = valueIsValid && (enabled == 0 || enabled == 1)
            && m_radioBackend->setPreampEnabled(enabled == 1);
    } else if (name == QStringLiteral("attenuator")) {
        updated = m_radioBackend->setAttenuatorDb(value.toInt(&valueIsValid));
        updated = updated && valueIsValid;
    } else if (name == QStringLiteral("audio_level")) {
        updated = m_radioBackend->setAudioLevel(value.toInt(&valueIsValid));
        updated = updated && valueIsValid;
    } else if (name == QStringLiteral("audio_mute")) {
        const int muted = value.toInt(&valueIsValid);
        if (valueIsValid && (muted == 0 || muted == 1)) {
            m_radioBackend->setAudioMuted(muted == 1);
            updated = true;
        }
    } else if (name == QStringLiteral("tx")) {
        const int assigned = value.toInt(&valueIsValid);
        if (valueIsValid && (assigned == 0 || assigned == 1)) {
            m_txAssigned = assigned == 1;
            if (!m_txAssigned) {
                m_transmitting = false;
            }
            updated = true;
        }
    } else {
        sendUnsupportedCommand(socket, sequence, command);
        return true;
    }
    if (!updated) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid slice setting"));
        return true;
    }
    scheduleSettingsSave();
    sendSliceStatus(socket);
    sendResponse(socket, sequence, 0);
    return true;
}

bool FlexControlServer::processFilter(QTcpSocket* socket, quint32 sequence,
                                      const QString& command)
{
    if (!command.startsWith(QStringLiteral("filt "))) {
        return false;
    }
    const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool lowIsValid = false;
    bool highIsValid = false;
    if (parts.size() != 4 || parts.at(1) != QStringLiteral("0")) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid filter"));
        return true;
    }
    const int lowHz = parts.at(2).toInt(&lowIsValid);
    const int highHz = parts.at(3).toInt(&highIsValid);
    if (!lowIsValid || !highIsValid || !m_radioBackend->setFilter(lowHz, highHz)) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid filter"));
        return true;
    }
    scheduleSettingsSave();
    sendSliceStatus(socket);
    sendResponse(socket, sequence, 0);
    return true;
}

bool FlexControlServer::processClientCommand(QTcpSocket* socket, quint32 sequence,
                                             const QString& command)
{
    if (!command.startsWith(QStringLiteral("client "))) {
        return false;
    }
    if (command.startsWith(QStringLiteral("client set "))) {
        const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& part : parts) {
            if (!part.startsWith(QStringLiteral("network_mtu="))) {
                continue;
            }
            bool valid = false;
            const int mtu = part.mid(12).toInt(&valid);
            if (!valid) {
                sendResponse(socket, sequence, kInvalidArgumentError,
                             QStringLiteral("Invalid network MTU"));
                return true;
            }
            const int boundedMtu = qBound(kMinimumNetworkMtu, mtu, kMaximumNetworkMtu);
            if (m_networkMtu != boundedMtu) {
                m_networkMtu = boundedMtu;
                emit networkMtuChanged(m_networkMtu);
                scheduleSettingsSave();
            }
            sendResponse(socket, sequence, 0, QString::number(m_networkMtu));
            return true;
        }
        sendResponse(socket, sequence, 0);
        return true;
    }
    if (command.startsWith(QStringLiteral("client program "))
        || command.startsWith(QStringLiteral("client low_bw_connect"))
        || command.startsWith(QStringLiteral("client station "))) {
        sendResponse(socket, sequence, 0);
        return true;
    }
    if (command == QStringLiteral("client ip")) {
        sendResponse(socket, sequence, 0, socket->peerAddress().toString());
        return true;
    }
    sendUnsupportedCommand(socket, sequence, command);
    return true;
}

bool FlexControlServer::processKeepaliveCommand(QTcpSocket* socket, quint32 sequence,
                                                const QString& command)
{
    if (command == QStringLiteral("ping")) {
        sendResponse(socket, sequence, 0);
        return true;
    }
    if (!command.startsWith(QStringLiteral("keepalive "))) {
        return false;
    }
    if (command == QStringLiteral("keepalive enable")
        || command == QStringLiteral("keepalive disable")
        || command == QStringLiteral("keepalive ping")) {
        sendResponse(socket, sequence, 0);
        return true;
    }
    sendUnsupportedCommand(socket, sequence, command);
    return true;
}

bool FlexControlServer::processTransmitCommand(QTcpSocket* socket, quint32 sequence,
                                               const QString& command)
{
    QString value;
    if (command.startsWith(QStringLiteral("xmit "))) {
        value = command.mid(5).trimmed();
    } else if (command.startsWith(QStringLiteral("transmit tune "))) {
        value = command.mid(14).trimmed();
    } else {
        return false;
    }

    if (value != QStringLiteral("0") && value != QStringLiteral("1")) {
        sendResponse(socket, sequence, kInvalidArgumentError, QStringLiteral("Invalid transmit state"));
        return true;
    }
    if (value == QStringLiteral("1") && !m_txAssigned) {
        sendResponse(socket, sequence, kUnsupportedCommandError,
                     QStringLiteral("No transmit slice assigned"));
        return true;
    }

    // The test backend reports Flex TX state but never keys physical hardware.
    m_transmitting = value == QStringLiteral("1");
    sendTransmitStatus(socket);
    sendResponse(socket, sequence, 0);
    return true;
}

bool FlexControlServer::processSubscription(QTcpSocket* socket, quint32 sequence,
                                            const QString& command)
{
    if (!command.startsWith(QStringLiteral("sub "))) {
        return false;
    }
    if (command.startsWith(QStringLiteral("sub radio"))) {
        sendRadioStatus(socket);
    } else if (command.startsWith(QStringLiteral("sub slice"))) {
        sendSliceStatus(socket);
    } else if (command.startsWith(QStringLiteral("sub pan"))) {
        sendPanStatus(socket);
    } else if (command.startsWith(QStringLiteral("sub meter"))) {
        sendMeterStatus(socket);
    } else if (command.startsWith(QStringLiteral("sub tx"))) {
        sendTransmitStatus(socket);
    } else if (command.startsWith(QStringLiteral("sub atu"))
        || command.startsWith(QStringLiteral("sub atu"))
        || command.startsWith(QStringLiteral("sub amplifier"))
        || command.startsWith(QStringLiteral("sub audio"))
        || command.startsWith(QStringLiteral("sub gps"))
        || command.startsWith(QStringLiteral("sub apd"))
        || command.startsWith(QStringLiteral("sub client"))
        || command.startsWith(QStringLiteral("sub xvtr"))
        || command.startsWith(QStringLiteral("sub tnf"))
        || command.startsWith(QStringLiteral("sub memories"))
        || command.startsWith(QStringLiteral("sub cwx"))
        || command.startsWith(QStringLiteral("sub dax"))
        || command.startsWith(QStringLiteral("sub daxiq"))
        || command.startsWith(QStringLiteral("sub codec"))
        || command.startsWith(QStringLiteral("sub dvk"))
        || command.startsWith(QStringLiteral("sub navtex"))
        || command.startsWith(QStringLiteral("sub usb_cable"))
        || command.startsWith(QStringLiteral("sub spot"))
        || command.startsWith(QStringLiteral("sub waveform"))
        || command.startsWith(QStringLiteral("sub license"))) {
        // AetherSDR subscribes to these optional Flex objects at startup.
        // This one-receiver gateway has no state to publish for them yet.
    } else {
        sendUnsupportedCommand(socket, sequence, command);
        return true;
    }
    sendResponse(socket, sequence, 0);
    return true;
}

bool FlexControlServer::processDisplayCommand(QTcpSocket* socket, quint32 sequence,
                                              const QString& command)
{
    if (!command.startsWith(QStringLiteral("display "))) {
        return false;
    }
    if (command == QStringLiteral("display pan rfgain_info 0x40000000")) {
        sendPanStatus(socket);
        sendResponse(socket, sequence, 0);
        return true;
    }
    if (command.startsWith(QStringLiteral("display pan set 0x40000000"))) {
        const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        double centerFrequencyMhz = m_panUpdatePending
            ? m_pendingPanCenterFrequencyMhz : m_radioBackend->panCenterFrequencyMhz();
        int bandwidthHz = m_panUpdatePending
            ? m_pendingPanBandwidthHz : m_radioBackend->panBandwidthHz();
        bool panViewChanged = false;
        double minimumDbm = m_panMinimumDbm;
        double maximumDbm = m_panMaximumDbm;
        int rfGain = m_panRfGain;
        int spectrumPoints = m_panSpectrumPoints;
        int spectrumFps = m_spectrumFps;
        for (const QString& part : parts) {
            if (part.startsWith(QStringLiteral("center="))) {
                bool centerIsValid = false;
                centerFrequencyMhz = part.mid(7).toDouble(&centerIsValid);
                if (!centerIsValid || centerFrequencyMhz <= 0.0) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan center frequency"));
                    return true;
                }
                panViewChanged = true;
            } else if (part.startsWith(QStringLiteral("bandwidth="))) {
                bool bandwidthIsValid = false;
                const double bandwidthMhz = part.mid(10).toDouble(&bandwidthIsValid);
                bandwidthHz = qRound(bandwidthMhz * 1000000.0);
                if (!bandwidthIsValid || bandwidthHz <= 0) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan bandwidth"));
                    return true;
                }
                panViewChanged = true;
            } else if (part.startsWith(QStringLiteral("min_dbm="))) {
                bool valueIsValid = false;
                minimumDbm = part.mid(8).toDouble(&valueIsValid);
                if (!valueIsValid) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan minimum level"));
                    return true;
                }
            } else if (part.startsWith(QStringLiteral("max_dbm="))) {
                bool valueIsValid = false;
                maximumDbm = part.mid(8).toDouble(&valueIsValid);
                if (!valueIsValid) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan maximum level"));
                    return true;
                }
            } else if (part.startsWith(QStringLiteral("rfgain="))) {
                bool valueIsValid = false;
                rfGain = part.mid(7).toInt(&valueIsValid);
                if (!valueIsValid) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan RF gain"));
                    return true;
                }
            } else if (part.startsWith(QStringLiteral("xpixels="))) {
                bool valueIsValid = false;
                spectrumPoints = part.mid(8).toInt(&valueIsValid);
                if (!valueIsValid || spectrumPoints < 128 || spectrumPoints > 8192) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid pan pixel count"));
                    return true;
                }
            } else if (part.startsWith(QStringLiteral("fps="))) {
                bool valueIsValid = false;
                spectrumFps = part.mid(4).toInt(&valueIsValid);
                if (!valueIsValid || spectrumFps < 1) {
                    sendResponse(socket, sequence, kInvalidArgumentError,
                                 QStringLiteral("Invalid spectrum FPS"));
                    return true;
                }
                spectrumFps = qMin(spectrumFps, kMaximumSpectrumFps);
            }
        }
        if (minimumDbm >= maximumDbm) {
            sendResponse(socket, sequence, kInvalidArgumentError,
                         QStringLiteral("Invalid pan level range"));
            return true;
        }
        if (!m_radioBackend->setSpectrumDbmRange(
                static_cast<float>(minimumDbm), static_cast<float>(maximumDbm))) {
            sendResponse(socket, sequence, kInvalidArgumentError,
                         QStringLiteral("Cannot set pan level range"));
            return true;
        }
        m_panMinimumDbm = minimumDbm;
        m_panMaximumDbm = maximumDbm;
        m_panRfGain = rfGain;
        if (!m_radioBackend->setSpectrumPointCount(spectrumPoints)) {
            sendResponse(socket, sequence, kInvalidArgumentError,
                         QStringLiteral("Cannot set pan pixel count"));
            return true;
        }
        m_panSpectrumPoints = spectrumPoints;
        if (m_spectrumFps != spectrumFps) {
            m_spectrumFps = spectrumFps;
            emit spectrumFpsChanged(m_spectrumFps);
        }
        if (panViewChanged) {
            m_pendingPanCenterFrequencyMhz = centerFrequencyMhz;
            m_pendingPanBandwidthHz = bandwidthHz;
            m_panUpdatePending = true;
            m_panUpdateTimer->start(kPanUpdateDelayMs);
        }
        scheduleSettingsSave();
        if (!panViewChanged) {
            sendPanStatus(socket);
        }
        sendResponse(socket, sequence, 0, QStringLiteral("40000000"));
        return true;
    }
    if (command.startsWith(QStringLiteral("display pan create"))
        || command.startsWith(QStringLiteral("display panafall create"))) {
        sendPanStatus(socket);
        sendResponse(socket, sequence, 0, QStringLiteral("40000000"));
        return true;
    }
    if (command.startsWith(QStringLiteral("display panafall set 0x42000000"))) {
        const QStringList parts = command.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int blackLevel = m_waterfallBlackLevel;
        int colorGain = m_waterfallColorGain;
        bool autoBlack = m_waterfallAutoBlack;
        int waterfallRate = m_waterfallRate;
        for (const QString& part : parts) {
            if (part.startsWith(QStringLiteral("black_level="))) {
                blackLevel = part.mid(12).toInt();
            } else if (part.startsWith(QStringLiteral("color_gain="))) {
                colorGain = part.mid(11).toInt();
            } else if (part.startsWith(QStringLiteral("auto_black="))) {
                autoBlack = part.mid(11) != QStringLiteral("0");
            } else if (part.startsWith(QStringLiteral("line_duration="))) {
                waterfallRate = part.mid(14).toInt();
            }
        }
        m_waterfallBlackLevel = blackLevel;
        m_waterfallColorGain = colorGain;
        m_waterfallAutoBlack = autoBlack;
        waterfallRate = qBound(kMinimumWaterfallRate, waterfallRate,
                               kMaximumWaterfallRate);
        if (m_waterfallRate != waterfallRate) {
            m_waterfallRate = waterfallRate;
            emit waterfallRateChanged(m_waterfallRate);
        }
        scheduleSettingsSave();
        sendPanStatus(socket);
        sendResponse(socket, sequence, 0);
        return true;
    }
    sendUnsupportedCommand(socket, sequence, command);
    return true;
}

bool FlexControlServer::processStreamCommand(QTcpSocket* socket, quint32 sequence,
                                             const QString& command)
{
    if (!command.startsWith(QStringLiteral("stream "))) {
        return false;
    }
    if (command.startsWith(QStringLiteral("stream create type=remote_audio_rx"))) {
        const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
        sendLine(socket, QStringLiteral("S%1|stream 0x43000000 type=remote_audio_rx "
                                        "client_handle=0x%1 compression=none").arg(handleText));
        sendResponse(socket, sequence, 0, QStringLiteral("43000000"));
        return true;
    }
    if (command.startsWith(QStringLiteral("stream remove 0x43000000"))
        || command.startsWith(QStringLiteral("stream set 0x43000000"))) {
        sendResponse(socket, sequence, 0);
        return true;
    }
    sendUnsupportedCommand(socket, sequence, command);
    return true;
}

void FlexControlServer::sendUnsupportedCommand(QTcpSocket* socket, quint32 sequence,
                                               const QString& command) const
{
    qWarning().noquote() << "Unsupported Flex command:" << command;
    sendResponse(socket, sequence, kUnsupportedCommandError,
                 QStringLiteral("Unsupported command"));
}

void FlexControlServer::sendLine(QTcpSocket* socket, const QString& line) const
{
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(line.toUtf8());
        socket->write("\n");
    }
}

void FlexControlServer::sendResponse(QTcpSocket* socket, quint32 sequence,
                                     quint32 code, const QString& body) const
{
    QString response = QStringLiteral("R%1|%2|").arg(sequence).arg(code, 0, 16).toUpper();
    response += body;
    sendLine(socket, response);
}

void FlexControlServer::sendInitialStatus(QTcpSocket* socket)
{
    sendRadioStatus(socket);
    sendSliceStatus(socket);
    sendPanStatus(socket);
}

void FlexControlServer::sendRadioStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    sendLine(socket, QStringLiteral("S%1|radio slices=2 panadapters=2 nickname=HiQSDR "
                                    "callsign=HIQSDR model=FLEX-8400 mf_enable=1").arg(handleText));
}

void FlexControlServer::sendSliceStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    sendLine(socket, QStringLiteral("S%1|slice 0 client_handle=0x%1 pan=0x40000000 "
                                    "RF_frequency=%2 mode=%3 filter_lo=%4 filter_hi=%5 "
                                    "rxant=%6 preamp=%7 attenuator=%8 audio_level=%9 "
                                    "audio_mute=%10 agc_mode=%11 agc_threshold=%12 tx=%13 "
                                    "tx_client_handle=0x%1 in_use=1 active=1")
                         .arg(handleText)
                         .arg(m_radioBackend->sliceFrequencyMhz(), 0, 'f', 6)
                         .arg(m_radioBackend->sliceMode())
                         .arg(m_radioBackend->filterLowHz())
                         .arg(m_radioBackend->filterHighHz())
                         .arg(m_radioBackend->antenna())
                         .arg(m_radioBackend->preampEnabled() ? 1 : 0)
                         .arg(m_radioBackend->attenuatorDb())
                         .arg(m_radioBackend->audioLevel())
                         .arg(m_radioBackend->audioMuted() ? 1 : 0)
                         .arg(m_radioBackend->agcMode())
                         .arg(m_radioBackend->agcThreshold())
                         .arg(m_txAssigned ? 1 : 0));
}

void FlexControlServer::sendPanStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    sendLine(socket, QStringLiteral("S%1|display pan 0x40000000 client_handle=0x%1 "
                                    "waterfall=0x42000000 center=%2 bandwidth=%3 "
                                    "min_dbm=%4 max_dbm=%5 rfgain=%6 "
                                    "rfgain_low=-8 rfgain_high=32 x_pixels=%7 y_pixels=700 "
                                    "fps=%8 ant_list=ANT1")
                         .arg(handleText)
                         .arg(m_radioBackend->panCenterFrequencyMhz(), 0, 'f', 6)
                         .arg(static_cast<double>(m_radioBackend->panBandwidthHz()) / 1000000.0,
                              0, 'f', 6)
                         .arg(m_panMinimumDbm, 0, 'f', 2)
                         .arg(m_panMaximumDbm, 0, 'f', 2)
                         .arg(m_panRfGain)
                         .arg(m_panSpectrumPoints)
                         .arg(m_spectrumFps));
    sendLine(socket, QStringLiteral("S%1|display waterfall 0x42000000 client_handle=0x%1 "
                                    "panadapter=0x40000000 line_duration=%2 auto_black=%3 "
                                    "black_level=%4 color_gain=%5")
                         .arg(handleText)
                         .arg(m_waterfallRate)
                         .arg(m_waterfallAutoBlack ? 1 : 0)
                         .arg(m_waterfallBlackLevel)
                         .arg(m_waterfallColorGain));
}

void FlexControlServer::sendPanStatusToClients() const
{
    for (QTcpSocket* socket : m_clientHandles.keys()) {
        sendPanStatus(socket);
    }
}

void FlexControlServer::sendMeterStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    sendLine(socket, QStringLiteral("S%1|meter 1.src=SLC#1.num=0#1.nam=LEVEL#"
                                    "1.unit=dBm#1.low=-150.0#1.hi=20.0#"
                                    "1.desc=Slice A signal level")
                         .arg(handleText));
}

void FlexControlServer::sendTransmitStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    const QString state = m_transmitting ? QStringLiteral("TRANSMITTING")
                                         : QStringLiteral("READY");
    sendLine(socket, QStringLiteral("S%1|interlock state=%2 tx_client_handle=0x%1 source=SW")
                         .arg(handleText, state));
}

void FlexControlServer::sendDiscoveryPacket()
{
    const QString address = m_listenAddress == QHostAddress::AnyIPv4
        ? QStringLiteral("127.0.0.1") : m_listenAddress.toString();
    const QString packet = QStringLiteral("name=%1 model=%2 serial=%3 version=%4 ip=%5 "
                                          "port=%6 status=Available mf_enable=1")
                               .arg(kRadioName, kModel, kSerial, kVersion, address)
                               .arg(m_port);
    m_discoverySocket.writeDatagram(packet.toUtf8(), QHostAddress::Broadcast, 4992);
    // macOS does not always loop a broadcast packet back to local listeners.
    m_discoverySocket.writeDatagram(packet.toUtf8(), QHostAddress::LocalHost, 4992);
}

void FlexControlServer::loadSettings()
{
    const QString directory = QDir::current().filePath(QStringLiteral("settings"));
    QSettings settings(directory + QStringLiteral("/hiqsdr-flex6xxx.ini"),
                       QSettings::IniFormat);
    const double minimumDbm = settings.value(QStringLiteral("display/minimum_dbm"),
                                             m_panMinimumDbm).toDouble();
    const double maximumDbm = settings.value(QStringLiteral("display/maximum_dbm"),
                                             m_panMaximumDbm).toDouble();
    if (minimumDbm < maximumDbm) {
        m_panMinimumDbm = minimumDbm;
        m_panMaximumDbm = maximumDbm;
    }
    m_panRfGain = settings.value(QStringLiteral("display/rf_gain"), m_panRfGain).toInt();
    m_panSpectrumPoints = qBound(128,
        settings.value(QStringLiteral("display/spectrum_points"), m_panSpectrumPoints).toInt(),
        8192);
    m_radioBackend->setSpectrumPointCount(m_panSpectrumPoints);
    m_spectrumFps = qBound(1, settings.value(QStringLiteral("display/fps"),
                                               m_spectrumFps).toInt(), kMaximumSpectrumFps);
    m_waterfallBlackLevel = settings.value(QStringLiteral("waterfall/black_level"),
                                            m_waterfallBlackLevel).toInt();
    m_waterfallColorGain = settings.value(QStringLiteral("waterfall/color_gain"),
                                           m_waterfallColorGain).toInt();
    m_waterfallAutoBlack = settings.value(QStringLiteral("waterfall/auto_black"),
                                           m_waterfallAutoBlack).toBool();
    m_waterfallRate = qBound(kMinimumWaterfallRate, kMaximumWaterfallRate,
        settings.value(QStringLiteral("waterfall/rate"), m_waterfallRate).toInt());
    m_networkMtu = qBound(kMinimumNetworkMtu, kMaximumNetworkMtu,
        settings.value(QStringLiteral("network/mtu"), m_networkMtu).toInt());
    m_radioBackend->setSliceFrequencyMhz(
        settings.value(QStringLiteral("radio/slice_frequency_mhz"),
                       m_radioBackend->sliceFrequencyMhz()).toDouble());
    m_radioBackend->setSliceMode(settings.value(QStringLiteral("radio/slice_mode"),
                                                m_radioBackend->sliceMode()).toString());
    m_radioBackend->setAgcMode(settings.value(QStringLiteral("radio/agc_mode"),
                                              m_radioBackend->agcMode()).toString());
    m_radioBackend->setAgcThreshold(settings.value(QStringLiteral("radio/agc_threshold"),
                                                   m_radioBackend->agcThreshold()).toInt());
    m_radioBackend->setPanCenterFrequencyMhz(
        settings.value(QStringLiteral("radio/pan_center_frequency_mhz"),
                       m_radioBackend->panCenterFrequencyMhz()).toDouble());
    m_radioBackend->setPanBandwidthHz(
        settings.value(QStringLiteral("radio/pan_bandwidth_hz"),
                       m_radioBackend->panBandwidthHz()).toInt());
    m_radioBackend->setFilter(
        settings.value(QStringLiteral("radio/filter_low_hz"),
                       m_radioBackend->filterLowHz()).toInt(),
        settings.value(QStringLiteral("radio/filter_high_hz"),
                       m_radioBackend->filterHighHz()).toInt());
    m_radioBackend->setAntenna(settings.value(QStringLiteral("radio/antenna"),
                                              m_radioBackend->antenna()).toString());
    m_radioBackend->setPreampEnabled(settings.value(QStringLiteral("radio/preamp"),
                                                     m_radioBackend->preampEnabled()).toBool());
    m_radioBackend->setAttenuatorDb(settings.value(QStringLiteral("radio/attenuator_db"),
                                                    m_radioBackend->attenuatorDb()).toInt());
    m_radioBackend->setAudioLevel(settings.value(QStringLiteral("radio/audio_level"),
                                                 m_radioBackend->audioLevel()).toInt());
    m_radioBackend->setAudioMuted(settings.value(QStringLiteral("radio/audio_muted"),
                                                 m_radioBackend->audioMuted()).toBool());
    loadDisplayProfile();
    qInfo() << "Loaded display range:" << m_panMinimumDbm << "to" << m_panMaximumDbm;
}

void FlexControlServer::loadDisplayProfile()
{
    const QString directory = QDir::current().filePath(QStringLiteral("settings"));
    QSettings settings(directory + QStringLiteral("/hiqsdr-flex6xxx.ini"),
                       QSettings::IniFormat);
    const QString prefix = QStringLiteral("display_profiles/%1/")
        .arg(m_radioBackend->spectrumSampleRateHz());
    if (!settings.contains(prefix + QStringLiteral("minimum_dbm"))) {
        return;
    }

    const double minimumDbm = settings.value(prefix + QStringLiteral("minimum_dbm")).toDouble();
    const double maximumDbm = settings.value(prefix + QStringLiteral("maximum_dbm")).toDouble();
    if (minimumDbm >= maximumDbm) {
        qWarning() << "Ignoring invalid display profile for sample rate"
                   << m_radioBackend->spectrumSampleRateHz();
        return;
    }

    m_panMinimumDbm = minimumDbm;
    m_panMaximumDbm = maximumDbm;
    m_panRfGain = settings.value(prefix + QStringLiteral("rf_gain"), m_panRfGain).toInt();
    m_radioBackend->setSpectrumDbmRange(
        static_cast<float>(m_panMinimumDbm), static_cast<float>(m_panMaximumDbm));
}

void FlexControlServer::saveSettings() const
{
    const QString directory = QDir::current().filePath(QStringLiteral("settings"));
    QDir().mkpath(directory);
    QSettings settings(directory + QStringLiteral("/hiqsdr-flex6xxx.ini"),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("display/minimum_dbm"), m_panMinimumDbm);
    settings.setValue(QStringLiteral("display/maximum_dbm"), m_panMaximumDbm);
    settings.setValue(QStringLiteral("display/rf_gain"), m_panRfGain);
    settings.setValue(QStringLiteral("display/spectrum_points"), m_panSpectrumPoints);
    settings.setValue(QStringLiteral("display/fps"), m_spectrumFps);
    const QString profilePrefix = QStringLiteral("display_profiles/%1/")
        .arg(m_radioBackend->spectrumSampleRateHz());
    settings.setValue(profilePrefix + QStringLiteral("minimum_dbm"), m_panMinimumDbm);
    settings.setValue(profilePrefix + QStringLiteral("maximum_dbm"), m_panMaximumDbm);
    settings.setValue(profilePrefix + QStringLiteral("rf_gain"), m_panRfGain);
    settings.setValue(QStringLiteral("waterfall/black_level"), m_waterfallBlackLevel);
    settings.setValue(QStringLiteral("waterfall/color_gain"), m_waterfallColorGain);
    settings.setValue(QStringLiteral("waterfall/auto_black"), m_waterfallAutoBlack);
    settings.setValue(QStringLiteral("waterfall/rate"), m_waterfallRate);
    settings.setValue(QStringLiteral("network/mtu"), m_networkMtu);
    settings.setValue(QStringLiteral("radio/slice_frequency_mhz"),
                      m_radioBackend->sliceFrequencyMhz());
    settings.setValue(QStringLiteral("radio/slice_mode"), m_radioBackend->sliceMode());
    settings.setValue(QStringLiteral("radio/agc_mode"), m_radioBackend->agcMode());
    settings.setValue(QStringLiteral("radio/agc_threshold"), m_radioBackend->agcThreshold());
    settings.setValue(QStringLiteral("radio/pan_center_frequency_mhz"),
                      m_radioBackend->panCenterFrequencyMhz());
    settings.setValue(QStringLiteral("radio/pan_bandwidth_hz"),
                      m_radioBackend->panBandwidthHz());
    settings.setValue(QStringLiteral("radio/filter_low_hz"),
                      m_radioBackend->filterLowHz());
    settings.setValue(QStringLiteral("radio/filter_high_hz"),
                      m_radioBackend->filterHighHz());
    settings.setValue(QStringLiteral("radio/antenna"), m_radioBackend->antenna());
    settings.setValue(QStringLiteral("radio/preamp"), m_radioBackend->preampEnabled());
    settings.setValue(QStringLiteral("radio/attenuator_db"), m_radioBackend->attenuatorDb());
    settings.setValue(QStringLiteral("radio/audio_level"), m_radioBackend->audioLevel());
    settings.setValue(QStringLiteral("radio/audio_muted"), m_radioBackend->audioMuted());
    settings.sync();
}

void FlexControlServer::scheduleSettingsSave()
{
    m_settingsSaveTimer->start(kSettingsSaveDelayMs);
}

quint32 FlexControlServer::clientHandle(QTcpSocket* socket) const
{
    return m_clientHandles.value(socket);
}
