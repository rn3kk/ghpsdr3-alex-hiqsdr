#include "VitaUdpStreamer.h"

#include <QDebug>

VitaUdpStreamer::VitaUdpStreamer(QObject* parent)
    : QObject(parent)
{
}

void VitaUdpStreamer::send(const QByteArray& packet, const QList<UdpEndpoint>& endpoints)
{
    for (const UdpEndpoint& endpoint : endpoints) {
        if (!endpoint.address.isNull() && endpoint.port != 0) {
            const qint64 bytesWritten = m_socket.writeDatagram(packet, endpoint.address,
                                                                endpoint.port);
            if (bytesWritten != packet.size()) {
                qWarning() << "Cannot send VITA UDP packet to"
                           << endpoint.address.toString() << endpoint.port
                           << ":" << m_socket.errorString();
            }
        }
    }
}

void VitaUdpStreamer::send(const QList<QByteArray>& packets, const QList<UdpEndpoint>& endpoints)
{
    for (const QByteArray& packet : packets) {
        send(packet, endpoints);
    }
}
