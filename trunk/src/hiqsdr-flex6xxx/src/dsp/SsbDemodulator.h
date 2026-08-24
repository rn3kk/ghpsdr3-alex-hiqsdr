#pragma once

#include <QByteArray>
#include <QQueue>
#include <QVector>

#include <complex>

class SsbDemodulator
{
public:
    static constexpr int OutputSampleRate = 24000;

    enum class Sideband
    {
        Usb,
        Lsb
    };

    void setInputSampleRate(int sampleRate);
    void setFrequencyOffsetHz(double offsetHz);
    void setFilter(int lowHz, int highHz);
    void setSideband(Sideband sideband);
    void setAgcMode(const QString& mode);
    void setAgcThreshold(int threshold);
    void addIqDatagram(const QByteArray& datagram);
    QVector<float> takeAudio(int frameCount);
    QVector<float> takeAvailableAudio();

private:
    void addIqSample(float i, float q);
    void filterAndAppendAudio(float i, float q);
    void appendAudioSample(float sample);
    void updateFilter();
    qint32 readSigned24(const char* data) const;

    int m_inputSampleRate{96000};
    double m_frequencyOffsetHz{0.0};
    int m_filterLowHz{100};
    int m_filterHighHz{2900};
    Sideband m_sideband{Sideband::Usb};
    double m_oscillatorPhase{0.0};
    int m_resampleAccumulator{0};
    int m_resampleSampleCount{0};
    float m_resampleI{0.0f};
    float m_resampleQ{0.0f};
    QVector<std::complex<float>> m_filterCoefficients;
    QVector<std::complex<float>> m_filterSamples;
    QString m_agcMode{QStringLiteral("MED")};
    int m_agcThreshold{50};
    float m_agcEnvelope{0.0f};
    float m_agcGain{1.0f};
    QQueue<float> m_audioSamples;
};
