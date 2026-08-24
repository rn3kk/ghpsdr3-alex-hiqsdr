#pragma once

#include <QByteArray>
#include <QVector>

#include <complex>

#include "../RadioTypes.h"

class HiqSdrSpectrumProcessor
{
public:
    HiqSdrSpectrumProcessor();

    void setSampleRate(int sampleRate);
    void setPanBandwidthHz(int bandwidthHz);
    void setOutputPointCount(int pointCount);
    void setFrameRate(int framesPerSecond);
    void setCenterFrequencyHz(qint64 frequencyHz);
    void setDbmRange(float minimumDbm, float maximumDbm);
    void addIqDatagram(const QByteArray& datagram);
    bool hasSpectrumFrame() const;
    SpectrumFrame createSpectrumFrame();

private:
    void updateFftSize();
    void processFft();
    void transform(QVector<std::complex<float>>& samples) const;
    qint32 readSigned24(const char* data) const;

    QVector<std::complex<float>> m_samples;
    QVector<quint16> m_pixels;
    int m_fftSize{4096};
    int m_fftHopSize{3840};
    int m_frameRate{25};
    int m_sampleRate{240000};
    int m_panBandwidthHz{200000};
    qint64 m_centerFrequencyHz{14100000};
    float m_minimumDbm{-140.0f};
    float m_maximumDbm{-20.0f};
    quint32 m_frameIndex{0};
    bool m_frameReady{false};
};
