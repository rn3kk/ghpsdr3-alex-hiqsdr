#include "FlexControlServer.h"

#include <QDebug>
#include <QTimer>

#include "RadioBackend.h"

namespace {
const QString kModel = QStringLiteral("FLEX-8400");
const QString kSerial = QStringLiteral("HIQ84000001");
const QString kVersion = QStringLiteral("4.2.20.0");
const QString kRadioName = QStringLiteral("HiQSDR Flex Gateway");
constexpr quint32 kInvalidArgumentError = 0x50000002;
constexpr quint32 kUnsupportedCommandError = 0x50000015;
}

FlexControlServer::FlexControlServer(RadioBackend* radioBackend, QObject* parent)
    : QObject(parent),
      m_radioBackend(radioBackend),
      m_discoveryTimer(new QTimer(this))
{
    m_discoveryTimer->setInterval(1000);
    connect(&m_server, &QTcpServer::newConnection,
            this, &FlexControlServer::onNewConnection);
    connect(m_discoveryTimer, &QTimer::timeout,
            this, &FlexControlServer::onDiscoveryTimeout);
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
    m_readBuffers.remove(socket);
    m_clientHandles.remove(socket);
    m_clientUdpPorts.remove(socket);
    socket->deleteLater();
}

void FlexControlServer::onDiscoveryTimeout()
{
    sendDiscoveryPacket();
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
    if (command.startsWith(QStringLiteral("client program "))
        || command.startsWith(QStringLiteral("client low_bw_connect"))
        || command.startsWith(QStringLiteral("client station "))
        || command.startsWith(QStringLiteral("client set "))) {
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
    if (command.startsWith(QStringLiteral("display pan create"))
        || command.startsWith(QStringLiteral("display panafall create"))
        || command.startsWith(QStringLiteral("display pan set 0x40000000"))
        || command.startsWith(QStringLiteral("display panafall set 0x42000000"))) {
        sendPanStatus(socket);
        sendResponse(socket, sequence, 0, QStringLiteral("40000000"));
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
                                    "RF_frequency=%2 mode=USB filter_lo=%3 filter_hi=%4 "
                                    "rxant=%5 preamp=%6 attenuator=%7 audio_level=%8 "
                                    "audio_mute=%9 tx=%10 tx_client_handle=0x%1 in_use=1 active=1")
                         .arg(handleText)
                         .arg(m_radioBackend->sliceFrequencyMhz(), 0, 'f', 6)
                         .arg(m_radioBackend->filterLowHz())
                         .arg(m_radioBackend->filterHighHz())
                         .arg(m_radioBackend->antenna())
                         .arg(m_radioBackend->preampEnabled() ? 1 : 0)
                         .arg(m_radioBackend->attenuatorDb())
                         .arg(m_radioBackend->audioLevel())
                         .arg(m_radioBackend->audioMuted() ? 1 : 0)
                         .arg(m_txAssigned ? 1 : 0));
}

void FlexControlServer::sendPanStatus(QTcpSocket* socket) const
{
    const QString handleText = QString::number(clientHandle(socket), 16).toUpper();
    sendLine(socket, QStringLiteral("S%1|display pan 0x40000000 client_handle=0x%1 "
                                    "waterfall=0x42000000 center=14.100 bandwidth=0.200 "
                                    "min_dbm=-140 max_dbm=-20 x_pixels=1024 y_pixels=700 "
                                    "fps=20 ant_list=ANT1").arg(handleText));
    sendLine(socket, QStringLiteral("S%1|display waterfall 0x42000000 client_handle=0x%1 "
                                    "panadapter=0x40000000 line_duration=20 auto_black=1 "
                                    "black_level=15 color_gain=50").arg(handleText));
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
}

quint32 FlexControlServer::clientHandle(QTcpSocket* socket) const
{
    return m_clientHandles.value(socket);
}
