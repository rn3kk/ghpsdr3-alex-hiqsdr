#pragma once

struct IqSample
{
    float i;
    float q;
    float noiseI;
    float noiseQ;
};

class TestIqSource
{
public:
    static constexpr int SampleRate = 192000;

    IqSample nextSample();
    double signalFrequencyMhz() const;
    double panCenterFrequencyMhz() const;

private:
    double m_phase{0.0};
};
