#include "HiqSdrBackend.h"

#include "hiqsdr/HiqSdrDevice.h"

namespace {
constexpr int kHiqSdrSampleRate = 192000;
constexpr int kSpectrumBins = 1024;
constexpr quint16 kBlankSpectrumPixel = 700;
}

HiqSdrBackend::HiqSdrBackend(const QString& deviceAddress, QObject* parent)
    : RadioBackend(parent),
      m_device(new HiqSdrDevice(deviceAddress, this)),
      m_deviceAddress(deviceAddress)
{
}

HiqSdrBackend::~HiqSdrBackend()
{
    if (m_started) {
        m_device->close();
    }
}

bool HiqSdrBackend::start()
{
    const long long frequencyHz = qRound64(m_sliceFrequencyMhz * 1000000.0);
    if (!m_device->open(kHiqSdrSampleRate, frequencyHz)) {
        return false;
    }
    m_started = true;
    return sendInitialControlState();
}

bool HiqSdrBackend::setSliceFrequencyMhz(double frequencyMhz)
{
    if (frequencyMhz <= 0.0 || !m_started) {
        return false;
    }
    if (!m_device->setFrequency(qRound64(frequencyMhz * 1000000.0))) {
        return false;
    }
    if (!m_device->setPreselector(preselectorForFrequency(frequencyMhz))) {
        return false;
    }
    m_sliceFrequencyMhz = frequencyMhz;
    return true;
}

double HiqSdrBackend::sliceFrequencyMhz() const
{
    return m_sliceFrequencyMhz;
}

bool HiqSdrBackend::setFilter(int lowHz, int highHz)
{
    if (lowHz >= highHz) {
        return false;
    }
    // HiQSDR bandwidth is its IQ sample rate, not the receiver audio filter.
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    return true;
}

int HiqSdrBackend::filterLowHz() const
{
    return m_filterLowHz;
}

int HiqSdrBackend::filterHighHz() const
{
    return m_filterHighHz;
}

bool HiqSdrBackend::setAntenna(const QString& antenna)
{
    int input = -1;
    if (antenna == QStringLiteral("ANT1")) {
        input = 0;
    } else if (antenna == QStringLiteral("ANT2")) {
        input = 1;
    }
    if (input < 0 || !m_started || !m_device->setAntennaInput(input)) {
        return false;
    }
    m_antenna = antenna;
    return true;
}

QString HiqSdrBackend::antenna() const
{
    return m_antenna;
}

bool HiqSdrBackend::setPreampEnabled(bool enabled)
{
    if (!m_started || !m_device->setPreampEnabled(enabled)) {
        return false;
    }
    m_preampEnabled = enabled;
    return true;
}

bool HiqSdrBackend::preampEnabled() const
{
    return m_preampEnabled;
}

bool HiqSdrBackend::setAttenuatorDb(int attenuationDb)
{
    if (attenuationDb < 0 || attenuationDb > 44 || !m_started
        || !m_device->setAttenuatorDb(attenuationDb)) {
        return false;
    }
    m_attenuatorDb = attenuationDb;
    return true;
}

int HiqSdrBackend::attenuatorDb() const
{
    return m_attenuatorDb;
}

bool HiqSdrBackend::setAudioLevel(int level)
{
    if (level < 0 || level > 100) {
        return false;
    }
    m_audioLevel = level;
    return true;
}

int HiqSdrBackend::audioLevel() const
{
    return m_audioLevel;
}

void HiqSdrBackend::setAudioMuted(bool muted)
{
    m_audioMuted = muted;
}

bool HiqSdrBackend::audioMuted() const
{
    return m_audioMuted;
}

SpectrumFrame HiqSdrBackend::createSpectrumFrame()
{
    SpectrumFrame frame;
    frame.index = m_frameIndex++;
    frame.pixels.fill(kBlankSpectrumPixel, kSpectrumBins);
    return frame;
}

QVector<float> HiqSdrBackend::createUsbAudio(int frameCount)
{
    return QVector<float>(frameCount, 0.0f);
}

float HiqSdrBackend::filterLevelDbm() const
{
    return -150.0f;
}

bool HiqSdrBackend::sendInitialControlState()
{
    return setSliceFrequencyMhz(m_sliceFrequencyMhz)
        && setAntenna(m_antenna)
        && setPreampEnabled(m_preampEnabled)
        && setAttenuatorDb(m_attenuatorDb);
}

int HiqSdrBackend::preselectorForFrequency(double frequencyMhz) const
{
    // HiQSDR X1 low nibble: 0 is bypass, 1 through 10 select the fixed BPFs.
    if (frequencyMhz >= 1.800 && frequencyMhz < 2.000) {
        return 1;
    }
    if (frequencyMhz >= 3.500 && frequencyMhz < 4.000) {
        return 2;
    }
    if (frequencyMhz >= 7.000 && frequencyMhz < 7.300) {
        return 3;
    }
    if (frequencyMhz >= 10.100 && frequencyMhz < 10.150) {
        return 4;
    }
    if (frequencyMhz >= 14.000 && frequencyMhz < 14.350) {
        return 5;
    }
    if (frequencyMhz >= 18.068 && frequencyMhz < 18.168) {
        return 6;
    }
    if (frequencyMhz >= 21.000 && frequencyMhz < 21.450) {
        return 7;
    }
    if (frequencyMhz >= 24.890 && frequencyMhz < 24.990) {
        return 8;
    }
    if (frequencyMhz >= 28.000 && frequencyMhz < 29.700) {
        return 9;
    }
    if (frequencyMhz >= 50.000 && frequencyMhz < 54.000) {
        return 10;
    }
    return 0;
}
