#pragma once

#include <QVector>

#include "RadioTypes.h"
#include "TestIqSource.h"

class TestSignalProcessor
{
public:
    TestSignalProcessor();

    SpectrumFrame createSpectrumFrame();
    QVector<float> createUsbAudio(double sliceFrequencyMhz, int filterLowHz,
                                  int filterHighHz, int frameCount);
    float filterLevelDbm() const;

private:
    TestIqSource m_iqSource;
    quint32 m_frameIndex{0};
    double m_demodulatorPhase{0.0};
    float m_filterLevelDbm{-150.0f};
};
