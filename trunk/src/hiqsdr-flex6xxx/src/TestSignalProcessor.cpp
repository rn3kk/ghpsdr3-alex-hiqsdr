#include "TestSignalProcessor.h"

#include <QRandomGenerator>

#include <cmath>

namespace {
constexpr int kSpectrumBins = 1024;
constexpr int kSpectrumPixels = 700;
constexpr int kNoisePixel = 309;
constexpr int kCarrierPixel = 146;
constexpr double kPanBandwidthMhz = 0.200;
constexpr double kTwoPi = 6.28318530717958647692;
constexpr int kAudioDecimation = 8;
constexpr float kMeterCalibrationDb = -22.0f;
constexpr float kMinimumMeterLevelDbm = -150.0f;
}

TestSignalProcessor::TestSignalProcessor()
{
}

SpectrumFrame TestSignalProcessor::createSpectrumFrame()
{
    SpectrumFrame frame;
    frame.index = m_frameIndex++;
    frame.pixels.resize(kSpectrumBins);

    const double signalOffsetMhz = m_iqSource.signalFrequencyMhz()
        - (m_iqSource.panCenterFrequencyMhz() - kPanBandwidthMhz / 2.0);
    const int carrierBin = qRound(signalOffsetMhz / kPanBandwidthMhz * kSpectrumBins);

    for (int index = 0; index < kSpectrumBins; ++index) {
        int pixel = kNoisePixel + QRandomGenerator::global()->bounded(-7, 8);
        if (qAbs(index - carrierBin) <= 2) {
            pixel = kCarrierPixel + qAbs(index - carrierBin) * 12;
        }
        frame.pixels[index] = static_cast<quint16>(qBound(0, pixel, kSpectrumPixels - 1));
    }

    return frame;
}

QVector<float> TestSignalProcessor::createUsbAudio(double sliceFrequencyMhz, int filterLowHz,
                                                    int filterHighHz, int frameCount)
{
    QVector<float> audio;
    audio.resize(frameCount);

    const double vfoOffsetHz =
        (sliceFrequencyMhz - m_iqSource.panCenterFrequencyMhz()) * 1000000.0;
    const double audioOffsetHz =
        (m_iqSource.signalFrequencyMhz() - sliceFrequencyMhz) * 1000000.0;
    const bool signalInUsbFilter = audioOffsetHz >= filterLowHz
        && audioOffsetHz <= filterHighHz;
    const double demodulatorStep = kTwoPi * vfoOffsetHz / TestIqSource::SampleRate;

    double sumSquares = 0.0;
    for (int index = 0; index < frameCount; ++index) {
        float sample = 0.0f;
        for (int decimation = 0; decimation < kAudioDecimation; ++decimation) {
            const IqSample iq = m_iqSource.nextSample();
            const float localOscillatorI = static_cast<float>(std::cos(m_demodulatorPhase));
            const float localOscillatorQ = static_cast<float>(std::sin(m_demodulatorPhase));
            float filteredI = iq.noiseI * localOscillatorI + iq.noiseQ * localOscillatorQ;
            if (signalInUsbFilter) {
                filteredI = iq.i * localOscillatorI + iq.q * localOscillatorQ;
            }
            if (decimation == kAudioDecimation - 1) {
                sample = filteredI;
            }
            m_demodulatorPhase = std::fmod(m_demodulatorPhase + demodulatorStep, kTwoPi);
            if (m_demodulatorPhase < 0.0) {
                m_demodulatorPhase += kTwoPi;
            }
        }
        audio[index] = sample;
        sumSquares += static_cast<double>(sample) * sample;
    }

    const double rms = std::sqrt(sumSquares / frameCount);
    if (rms > 0.0) {
        m_filterLevelDbm = qMax(
            kMinimumMeterLevelDbm,
            static_cast<float>(20.0 * std::log10(rms) + kMeterCalibrationDb));
    } else {
        m_filterLevelDbm = kMinimumMeterLevelDbm;
    }

    return audio;
}

float TestSignalProcessor::filterLevelDbm() const
{
    return m_filterLevelDbm;
}
