#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QUdpSocket>

#include "RadioTypes.h"

class VitaUdpStreamer : public QObject
{
    Q_OBJECT

public:
    explicit VitaUdpStreamer(QObject* parent = nullptr);

    void send(const QByteArray& packet, const QList<UdpEndpoint>& endpoints);

private:
    QUdpSocket m_socket;
};
