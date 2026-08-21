#include "VitaUdpStreamer.h"

VitaUdpStreamer::VitaUdpStreamer(QObject* parent)
    : QObject(parent)
{
}

void VitaUdpStreamer::send(const QByteArray& packet, const QList<UdpEndpoint>& endpoints)
{
    for (const UdpEndpoint& endpoint : endpoints) {
        if (!endpoint.address.isNull() && endpoint.port != 0) {
            m_socket.writeDatagram(packet, endpoint.address, endpoint.port);
        }
    }
}
