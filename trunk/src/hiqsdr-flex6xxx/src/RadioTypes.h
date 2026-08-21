#pragma once

#include <QHostAddress>
#include <QVector>

struct SpectrumFrame
{
    QVector<quint16> pixels;
    quint32 index{0};
};

struct UdpEndpoint
{
    QHostAddress address;
    quint16 port{0};
};
