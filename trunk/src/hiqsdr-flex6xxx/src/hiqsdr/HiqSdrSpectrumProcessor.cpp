#include "HiqSdrSpectrumProcessor.h"

#include <QtMath>

#include <cmath>

namespace {
constexpr int kMinimumFftSize = 4096;
constexpr int kMaximumFftSize = 8192;
constexpr int kIqHeaderBytes = 2;
constexpr int kBytesPerIqSample = 6;
constexpr float kAdcScale = 8388608.0f;
constexpr float kSpectrumCalibrationDb = -20.0f;
constexpr float kMinimumMagnitude = 0.000000001f;
constexpr float kTwoPi = 6.28318530717958647692f;
}

HiqSdrSpectrumProcessor::HiqSdrSpectrumProcessor()
{
    m_pixels.fill(0, 700);
}

void HiqSdrSpectrumProcessor::setSampleRate(int sampleRate)
{
    m_sampleRate = sampleRate;
    updateFftSize();
    m_samples.clear();
    m_frameReady = false;
}

void HiqSdrSpectrumProcessor::setPanBandwidthHz(int bandwidthHz)
{
    m_panBandwidthHz = bandwidthHz;
    updateFftSize();
    m_samples.clear();
    m_frameReady = false;
}

void HiqSdrSpectrumProcessor::setOutputPointCount(int pointCount)
{
    if (pointCount < 128 || pointCount > 8192 || pointCount == m_pixels.size()) {
        return;
    }
    m_pixels.fill(0, pointCount);
    updateFftSize();
    m_samples.clear();
    m_frameReady = false;
}

void HiqSdrSpectrumProcessor::setFrameRate(int framesPerSecond)
{
    if (framesPerSecond < 1) {
        return;
    }
    m_frameRate = framesPerSecond;
    updateFftSize();
}

void HiqSdrSpectrumProcessor::setCenterFrequencyHz(qint64 frequencyHz)
{
    m_centerFrequencyHz = frequencyHz;
    m_samples.clear();
    m_frameReady = false;
}

void HiqSdrSpectrumProcessor::setDbmRange(float minimumDbm, float maximumDbm)
{
    if (minimumDbm < maximumDbm) {
        m_minimumDbm = minimumDbm;
        m_maximumDbm = maximumDbm;
    }
}

void HiqSdrSpectrumProcessor::addIqDatagram(const QByteArray& datagram)
{
    const char* data = datagram.constData() + kIqHeaderBytes;
    const int sampleCount = (datagram.size() - kIqHeaderBytes) / kBytesPerIqSample;
    for (int index = 0; index < sampleCount; ++index) {
        const float i = static_cast<float>(readSigned24(data)) / kAdcScale;
        const float q = static_cast<float>(readSigned24(data + 3)) / kAdcScale;
        m_samples.append(std::complex<float>(i, q));
        data += kBytesPerIqSample;
        if (m_samples.size() == m_fftSize) {
            processFft();
            m_samples.remove(0, m_fftHopSize);
        }
    }
}

void HiqSdrSpectrumProcessor::updateFftSize()
{
    const double requiredBins = static_cast<double>(m_sampleRate)
        / m_panBandwidthHz * m_pixels.size();
    int fftSize = kMinimumFftSize;
    while (fftSize < requiredBins && fftSize < kMaximumFftSize) {
        fftSize *= 2;
    }
    m_fftSize = fftSize;
    m_fftHopSize = qBound(1, m_sampleRate / m_frameRate, m_fftSize);
}

bool HiqSdrSpectrumProcessor::hasSpectrumFrame() const
{
    return m_frameReady;
}

SpectrumFrame HiqSdrSpectrumProcessor::createSpectrumFrame()
{
    SpectrumFrame frame;
    frame.index = m_frameIndex++;
    frame.pixels = m_pixels;
    frame.centerFrequencyHz = m_centerFrequencyHz;
    frame.bandwidthHz = m_panBandwidthHz;
    frame.minimumDbm = m_minimumDbm;
    frame.maximumDbm = m_maximumDbm;
    m_frameReady = false;
    return frame;
}

void HiqSdrSpectrumProcessor::processFft()
{
    QVector<std::complex<float>> bins;
    bins.resize(m_fftSize);
    float windowSum = 0.0f;
    for (int index = 0; index < m_fftSize; ++index) {
        const float window = 0.5f - 0.5f * std::cos(kTwoPi * index / (m_fftSize - 1));
        bins[index] = m_samples.at(index) * window;
        windowSum += window;
    }
    transform(bins);

    const int visibleBins = qBound(1, qRound(static_cast<double>(m_panBandwidthHz)
                                               / m_sampleRate * m_fftSize), m_fftSize);
    const float firstBin = (m_fftSize - visibleBins) / 2.0f;
    for (int pixel = 0; pixel < m_pixels.size(); ++pixel) {
        const float source = firstBin + (pixel + 0.5f) * visibleBins / m_pixels.size();
        const int bin = qBound(0, qRound(source), m_fftSize - 1);
        const int shiftedBin = (bin + m_fftSize / 2) % m_fftSize;
        const float magnitude = qMax(kMinimumMagnitude,
                                     std::abs(bins.at(shiftedBin)) / windowSum);
        const float dbm = 20.0f * std::log10(magnitude) + kSpectrumCalibrationDb;
        const int level = qBound(0, qRound((m_maximumDbm - dbm)
                                             / (m_maximumDbm - m_minimumDbm) * 699.0f),
                                 699);
        m_pixels[pixel] = static_cast<quint16>(level);
    }
    m_frameReady = true;
}

void HiqSdrSpectrumProcessor::transform(QVector<std::complex<float>>& samples) const
{
    for (int index = 1, reversed = 0; index < m_fftSize; ++index) {
        int bit = m_fftSize >> 1;
        while (reversed & bit) {
            reversed ^= bit;
            bit >>= 1;
        }
        reversed ^= bit;
        if (index < reversed) {
            std::swap(samples[index], samples[reversed]);
        }
    }

    for (int length = 2; length <= m_fftSize; length <<= 1) {
        const float angle = -kTwoPi / length;
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (int start = 0; start < m_fftSize; start += length) {
            std::complex<float> factor(1.0f, 0.0f);
            const int halfLength = length / 2;
            for (int offset = 0; offset < halfLength; ++offset) {
                const std::complex<float> even = samples[start + offset];
                const std::complex<float> odd = samples[start + offset + halfLength] * factor;
                samples[start + offset] = even + odd;
                samples[start + offset + halfLength] = even - odd;
                factor *= step;
            }
        }
    }
}

qint32 HiqSdrSpectrumProcessor::readSigned24(const char* data) const
{
    qint32 value = static_cast<unsigned char>(data[0])
        | (static_cast<qint32>(static_cast<unsigned char>(data[1])) << 8)
        | (static_cast<qint32>(static_cast<unsigned char>(data[2])) << 16);
    if (value & 0x00800000) {
        value |= 0xFF000000;
    }
    return value;
}
