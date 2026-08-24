#include "RadioBackend.h"

RadioBackend::RadioBackend(QObject* parent)
    : QObject(parent)
{
}

bool RadioBackend::hasSpectrumFrame() const
{
    return true;
}

int RadioBackend::spectrumSampleRateHz() const
{
    return panBandwidthHz();
}

bool RadioBackend::setSpectrumPointCount(int pointCount)
{
    return pointCount > 0;
}

void RadioBackend::setSpectrumFrameRate(int framesPerSecond)
{
    Q_UNUSED(framesPerSecond)
}

bool RadioBackend::setSpectrumDbmRange(float minimumDbm, float maximumDbm)
{
    return minimumDbm < maximumDbm;
}
