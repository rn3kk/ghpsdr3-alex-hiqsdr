#include "TestIqSource.h"

#include <QRandomGenerator>

#include <cmath>

namespace {
constexpr double kPanCenterFrequencyMhz = 14.100000;
constexpr double kSignalFrequencyMhz = 14.125000;
constexpr double kSignalOffsetHz =
    (kSignalFrequencyMhz - kPanCenterFrequencyMhz) * 1000000.0;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr float kNoiseAmplitude = 0.004f;
constexpr float kSignalAmplitude = 0.10f;
}

IqSample TestIqSource::nextSample()
{
    const float noiseI = static_cast<float>(QRandomGenerator::global()->generateDouble() * 2.0 - 1.0)
        * kNoiseAmplitude;
    const float noiseQ = static_cast<float>(QRandomGenerator::global()->generateDouble() * 2.0 - 1.0)
        * kNoiseAmplitude;

    IqSample sample;
    sample.noiseI = noiseI;
    sample.noiseQ = noiseQ;
    sample.i = noiseI + static_cast<float>(std::cos(m_phase)) * kSignalAmplitude;
    sample.q = noiseQ + static_cast<float>(std::sin(m_phase)) * kSignalAmplitude;

    m_phase += kTwoPi * kSignalOffsetHz / SampleRate;
    if (m_phase >= kTwoPi) {
        m_phase -= kTwoPi;
    }

    return sample;
}

double TestIqSource::signalFrequencyMhz() const
{
    return kSignalFrequencyMhz;
}

double TestIqSource::panCenterFrequencyMhz() const
{
    return kPanCenterFrequencyMhz;
}
