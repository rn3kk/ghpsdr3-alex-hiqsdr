#include "SsbDemodulator.h"

#include <QtMath>

#include <cmath>

namespace {
constexpr int kIqHeaderBytes = 2;
constexpr int kBytesPerIqSample = 6;
constexpr float kAdcScale = 8388608.0f;
constexpr int kMaximumQueuedSamples = SsbDemodulator::OutputSampleRate * 2;
constexpr int kFilterTapCount = 257;
constexpr double kTwoPi = 6.28318530717958647692;
}

void SsbDemodulator::setInputSampleRate(int sampleRate)
{
    if (sampleRate > 0) {
        m_inputSampleRate = sampleRate;
        m_resampleAccumulator = 0;
        m_resampleSampleCount = 0;
        m_resampleI = 0.0f;
        m_resampleQ = 0.0f;
    }
}

void SsbDemodulator::setFrequencyOffsetHz(double offsetHz)
{
    m_frequencyOffsetHz = offsetHz;
}

void SsbDemodulator::setFilter(int lowHz, int highHz)
{
    m_filterLowHz = qBound(-OutputSampleRate / 2 + 1, qMin(lowHz, highHz),
                           OutputSampleRate / 2 - 2);
    m_filterHighHz = qBound(m_filterLowHz + 1, qMax(lowHz, highHz),
                            OutputSampleRate / 2 - 1);
    updateFilter();
}

void SsbDemodulator::setSideband(Sideband sideband)
{
    m_sideband = sideband;
}

void SsbDemodulator::setAgcMode(const QString& mode)
{
    m_agcMode = mode;
}

void SsbDemodulator::setAgcThreshold(int threshold)
{
    m_agcThreshold = qBound(0, threshold, 100);
}

void SsbDemodulator::addIqDatagram(const QByteArray& datagram)
{
    if (datagram.size() <= kIqHeaderBytes) {
        return;
    }

    const char* data = datagram.constData() + kIqHeaderBytes;
    const int sampleCount = (datagram.size() - kIqHeaderBytes) / kBytesPerIqSample;
    for (int index = 0; index < sampleCount; ++index) {
        const float rawI = static_cast<float>(readSigned24(data)) / kAdcScale;
        const float rawQ = static_cast<float>(readSigned24(data + 3)) / kAdcScale;
        // HiQSDR's legacy DttSP path supplies Q as real and I as imaginary.
        addIqSample(rawQ, rawI);
        data += kBytesPerIqSample;
    }
}

QVector<float> SsbDemodulator::takeAudio(int frameCount)
{
    // Radio audio must be live: stale samples would make tuning sound delayed.
    while (m_audioSamples.size() > frameCount) {
        m_audioSamples.dequeue();
    }

    QVector<float> audio(frameCount, 0.0f);
    for (int index = 0; index < frameCount && !m_audioSamples.isEmpty(); ++index) {
        audio[index] = m_audioSamples.dequeue();
    }
    return audio;
}

QVector<float> SsbDemodulator::takeAvailableAudio()
{
    QVector<float> audio;
    audio.reserve(m_audioSamples.size());
    while (!m_audioSamples.isEmpty()) {
        audio.append(m_audioSamples.dequeue());
    }
    return audio;
}

void SsbDemodulator::addIqSample(float i, float q)
{
    const float oscillatorI = static_cast<float>(std::cos(m_oscillatorPhase));
    const float oscillatorQ = static_cast<float>(std::sin(m_oscillatorPhase));
    const float mixedI = i * oscillatorI + q * oscillatorQ;
    const float mixedQ = q * oscillatorI - i * oscillatorQ;

    m_resampleI += mixedI;
    m_resampleQ += mixedQ;
    ++m_resampleSampleCount;
    m_resampleAccumulator += OutputSampleRate;
    if (m_resampleAccumulator >= m_inputSampleRate) {
        m_resampleAccumulator -= m_inputSampleRate;
        filterAndAppendAudio(m_resampleI / m_resampleSampleCount,
                             m_resampleQ / m_resampleSampleCount);
        m_resampleI = 0.0f;
        m_resampleQ = 0.0f;
        m_resampleSampleCount = 0;
    }

    // The mixer below multiplies by exp(-j * phase), so phase must advance
    // in the same direction as the requested Slice offset.
    const double step = kTwoPi * m_frequencyOffsetHz / m_inputSampleRate;
    m_oscillatorPhase += step;
    if (m_oscillatorPhase > kTwoPi || m_oscillatorPhase < -kTwoPi) {
        m_oscillatorPhase = std::fmod(m_oscillatorPhase, kTwoPi);
    }
}

void SsbDemodulator::filterAndAppendAudio(float i, float q)
{
    m_filterSamples.append(std::complex<float>(i, q));
    if (m_filterSamples.size() > m_filterCoefficients.size()) {
        m_filterSamples.removeFirst();
    }

    std::complex<float> filtered(0.0f, 0.0f);
    for (int index = 0; index < m_filterSamples.size(); ++index) {
        filtered += m_filterSamples.at(m_filterSamples.size() - 1 - index)
            * m_filterCoefficients.at(index);
    }
    appendAudioSample(filtered.real());
}

void SsbDemodulator::appendAudioSample(float sample)
{
    if (m_agcMode != QStringLiteral("OFF")) {
        const float envelopeAlpha = 1.0f - std::exp(-1.0f / (0.01f * OutputSampleRate));
        m_agcEnvelope += envelopeAlpha * (std::abs(sample) - m_agcEnvelope);

        const float targetLevel = std::pow(10.0f,
            -static_cast<float>(m_agcThreshold) / 40.0f);
        const float desiredGain = qBound(0.01f, targetLevel / qMax(m_agcEnvelope, 0.000001f),
                                         100.0f);
        float timeConstantSeconds = 0.25f;
        if (m_agcMode == QStringLiteral("FAST")) {
            timeConstantSeconds = desiredGain < m_agcGain ? 0.002f : 0.08f;
        } else if (m_agcMode == QStringLiteral("SLOW")) {
            timeConstantSeconds = desiredGain < m_agcGain ? 0.01f : 0.8f;
        } else {
            timeConstantSeconds = desiredGain < m_agcGain ? 0.005f : 0.25f;
        }
        const float gainAlpha = 1.0f - std::exp(-1.0f
            / (timeConstantSeconds * OutputSampleRate));
        m_agcGain += gainAlpha * (desiredGain - m_agcGain);
        sample *= m_agcGain;
    } else {
        m_agcGain = 1.0f;
    }

    if (m_audioSamples.size() >= kMaximumQueuedSamples) {
        m_audioSamples.dequeue();
    }
    m_audioSamples.enqueue(qBound(-1.0f, sample, 1.0f));
}

qint32 SsbDemodulator::readSigned24(const char* data) const
{
    const quint32 value = (static_cast<quint32>(static_cast<uchar>(data[0])) << 16)
        | (static_cast<quint32>(static_cast<uchar>(data[1])) << 8)
        | static_cast<quint32>(static_cast<uchar>(data[2]));
    return (value & 0x800000U) != 0 ? static_cast<qint32>(value | 0xFF000000U)
                                     : static_cast<qint32>(value);
}

void SsbDemodulator::updateFilter()
{
    m_filterCoefficients.resize(kFilterTapCount);
    m_filterSamples.clear();

    const float centerHz = (m_filterLowHz + m_filterHighHz) * 0.5f;
    const float bandwidthHz = m_filterHighHz - m_filterLowHz;
    const int centerTap = kFilterTapCount / 2;
    float normalization = 0.0f;
    QVector<float> weights(kFilterTapCount);

    for (int index = 0; index < kFilterTapCount; ++index) {
        const int sampleOffset = index - centerTap;
        const float sinc = sampleOffset == 0
            ? bandwidthHz / OutputSampleRate
            : std::sin(static_cast<float>(kTwoPi * bandwidthHz * sampleOffset
                                          / (2.0 * OutputSampleRate)))
                / static_cast<float>((kTwoPi / 2.0) * sampleOffset);
        const float window = 0.54f - 0.46f * std::cos(
            static_cast<float>(kTwoPi * index / (kFilterTapCount - 1)));
        weights[index] = sinc * window;
        normalization += weights[index];
    }

    for (int index = 0; index < kFilterTapCount; ++index) {
        const int sampleOffset = index - centerTap;
        const float phase = static_cast<float>(kTwoPi * centerHz * sampleOffset
                                               / OutputSampleRate);
        m_filterCoefficients[index] = std::complex<float>(
            weights.at(index) * std::cos(phase) / normalization,
            weights.at(index) * std::sin(phase) / normalization);
    }
}
