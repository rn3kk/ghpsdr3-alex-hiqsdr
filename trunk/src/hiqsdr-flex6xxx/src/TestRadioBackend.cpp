#include "TestRadioBackend.h"

TestRadioBackend::TestRadioBackend(QObject* parent)
    : RadioBackend(parent)
{
}

bool TestRadioBackend::start()
{
    return true;
}

bool TestRadioBackend::setSliceFrequencyMhz(double frequencyMhz)
{
    if (frequencyMhz <= 0.0) {
        return false;
    }
    m_sliceFrequencyMhz = frequencyMhz;
    return true;
}

double TestRadioBackend::sliceFrequencyMhz() const
{
    return m_sliceFrequencyMhz;
}

bool TestRadioBackend::setSliceMode(const QString& mode)
{
    if (mode != QStringLiteral("USB") && mode != QStringLiteral("LSB")) {
        return false;
    }
    m_sliceMode = mode;
    return true;
}

QString TestRadioBackend::sliceMode() const
{
    return m_sliceMode;
}

bool TestRadioBackend::setAgcMode(const QString& mode)
{
    if (mode != QStringLiteral("OFF") && mode != QStringLiteral("FAST")
        && mode != QStringLiteral("MED") && mode != QStringLiteral("SLOW")) {
        return false;
    }
    m_agcMode = mode;
    return true;
}

QString TestRadioBackend::agcMode() const
{
    return m_agcMode;
}

bool TestRadioBackend::setAgcThreshold(int threshold)
{
    if (threshold < 0 || threshold > 100) {
        return false;
    }
    m_agcThreshold = threshold;
    return true;
}

int TestRadioBackend::agcThreshold() const
{
    return m_agcThreshold;
}

bool TestRadioBackend::setPanCenterFrequencyMhz(double frequencyMhz)
{
    if (frequencyMhz <= 0.0) {
        return false;
    }
    m_panCenterFrequencyMhz = frequencyMhz;
    return true;
}

double TestRadioBackend::panCenterFrequencyMhz() const
{
    return m_panCenterFrequencyMhz;
}

bool TestRadioBackend::setPanBandwidthHz(int bandwidthHz)
{
    if (bandwidthHz <= 0) {
        return false;
    }
    m_panBandwidthHz = bandwidthHz;
    return true;
}

int TestRadioBackend::panBandwidthHz() const
{
    return m_panBandwidthHz;
}

bool TestRadioBackend::setFilter(int lowHz, int highHz)
{
    if (lowHz >= highHz) {
        return false;
    }
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    return true;
}

int TestRadioBackend::filterLowHz() const
{
    return m_filterLowHz;
}

int TestRadioBackend::filterHighHz() const
{
    return m_filterHighHz;
}

bool TestRadioBackend::setAntenna(const QString& antenna)
{
    if (antenna != QStringLiteral("ANT1")) {
        return false;
    }
    m_antenna = antenna;
    return true;
}

QString TestRadioBackend::antenna() const
{
    return m_antenna;
}

bool TestRadioBackend::setPreampEnabled(bool enabled)
{
    m_preampEnabled = enabled;
    return true;
}

bool TestRadioBackend::preampEnabled() const
{
    return m_preampEnabled;
}

bool TestRadioBackend::setAttenuatorDb(int attenuationDb)
{
    if (attenuationDb < 0 || attenuationDb > 30) {
        return false;
    }
    m_attenuatorDb = attenuationDb;
    return true;
}

int TestRadioBackend::attenuatorDb() const
{
    return m_attenuatorDb;
}

bool TestRadioBackend::setAudioLevel(int level)
{
    if (level < 0 || level > 100) {
        return false;
    }
    m_audioLevel = level;
    return true;
}

int TestRadioBackend::audioLevel() const
{
    return m_audioLevel;
}

void TestRadioBackend::setAudioMuted(bool muted)
{
    m_audioMuted = muted;
}

bool TestRadioBackend::audioMuted() const
{
    return m_audioMuted;
}

SpectrumFrame TestRadioBackend::createSpectrumFrame()
{
    return m_signalProcessor.createSpectrumFrame();
}

QVector<float> TestRadioBackend::createUsbAudio(int frameCount)
{
    QVector<float> audio = m_signalProcessor.createUsbAudio(
        m_sliceFrequencyMhz, m_filterLowHz, m_filterHighHz, frameCount);
    const float gain = m_audioMuted ? 0.0f : static_cast<float>(m_audioLevel) / 100.0f;
    for (float& sample : audio) {
        sample *= gain;
    }
    return audio;
}

float TestRadioBackend::filterLevelDbm() const
{
    return m_signalProcessor.filterLevelDbm();
}
