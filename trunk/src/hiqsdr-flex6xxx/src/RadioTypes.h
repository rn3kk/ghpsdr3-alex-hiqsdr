#pragma once

#include <QHostAddress>
#include <QVector>

struct SpectrumFrame
{
    QVector<quint16> pixels;
    quint32 index{0};
    qint64 centerFrequencyHz{14100000};
    int bandwidthHz{200000};
    float minimumDbm{-140.0f};
    float maximumDbm{-20.0f};
};

struct UdpEndpoint
{
    QHostAddress address;
    quint16 port{0};
};
