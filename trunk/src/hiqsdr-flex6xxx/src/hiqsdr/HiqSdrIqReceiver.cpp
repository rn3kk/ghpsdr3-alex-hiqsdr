#include "HiqSdrIqReceiver.h"

#include <QByteArray>
#include <QDebug>
#include <QHostAddress>
#include <QUdpSocket>

namespace {
constexpr quint16 kIqPort = 48247;
constexpr int kIqDatagramSize = 1442;
constexpr int kMaximumDatagramsPerEvent = 8;
constexpr quint8 kStartStreamCommand = 0x72;
constexpr quint8 kStopStreamCommand = 0x73;
}

HiqSdrIqReceiver::HiqSdrIqReceiver(const QString& address, QObject* parent)
    : QObject(parent),
      m_socket(new QUdpSocket(this)),
      m_address(address)
{
    connect(m_socket, &QUdpSocket::readyRead, this, &HiqSdrIqReceiver::onReadyRead);
}

bool HiqSdrIqReceiver::start()
{
    if (m_running) {
        return true;
    }
    if (QHostAddress(m_address).isNull()) {
        qWarning() << "Invalid HiQSDR IQ address:" << m_address;
        return false;
    }
    if (!m_socket->bind(QHostAddress::AnyIPv4, kIqPort,
                        QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) {
        qWarning() << "Cannot bind HiQSDR IQ port" << kIqPort << m_socket->errorString();
        return false;
    }

    m_socket->connectToHost(m_address, kIqPort);
    if (!m_socket->waitForConnected(1000)) {
        qWarning() << "Cannot connect HiQSDR IQ socket:" << m_socket->errorString();
        m_socket->close();
        return false;
    }
    if (!sendStreamCommand(kStartStreamCommand)) {
        m_socket->close();
        return false;
    }

    m_reportedInvalidDatagram = false;
    m_running = true;
    qInfo() << "HiQSDR IQ receiver started on UDP port" << kIqPort;
    return true;
}

void HiqSdrIqReceiver::stop()
{
    if (!m_running) {
        return;
    }
    sendStreamCommand(kStopStreamCommand);
    m_socket->disconnectFromHost();
    m_socket->close();
    m_running = false;
    qInfo() << "HiQSDR IQ receiver stopped";
}

bool HiqSdrIqReceiver::isRunning() const
{
    return m_running;
}

void HiqSdrIqReceiver::onReadyRead()
{
    int datagramsProcessed = 0;
    while (m_socket->hasPendingDatagrams()
           && datagramsProcessed < kMaximumDatagramsPerEvent) {
        const qint64 size = m_socket->pendingDatagramSize();
        QByteArray datagram;
        datagram.resize(static_cast<int>(size));
        if (m_socket->readDatagram(datagram.data(), datagram.size()) != size) {
            qWarning() << "Cannot read HiQSDR IQ datagram:" << m_socket->errorString();
            continue;
        }
        if (datagram.size() != kIqDatagramSize) {
            if (!m_reportedInvalidDatagram) {
                qWarning() << "Unexpected HiQSDR IQ datagram size:" << datagram.size();
                m_reportedInvalidDatagram = true;
            }
            continue;
        }
        emit iqDatagramReceived(datagram);
        ++datagramsProcessed;
    }
}

bool HiqSdrIqReceiver::sendStreamCommand(quint8 command)
{
    const QByteArray packet(2, static_cast<char>(command));
    if (m_socket->write(packet) != packet.size()) {
        qWarning() << "Cannot send HiQSDR IQ stream command:" << m_socket->errorString();
        return false;
    }
    return true;
}
