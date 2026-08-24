#include "HiqSdrBackend.h"

#include <QDebug>

#include "hiqsdr/HiqSdrDevice.h"
#include "hiqsdr/HiqSdrIqReceiver.h"
#include "dsp/AudioDspWorker.h"
#include "dsp/SsbDemodulator.h"
#include "hiqsdr/SpectrumDspWorker.h"

namespace {
constexpr int kMaximumPanBandwidthHz = 960000;
}

HiqSdrBackend::HiqSdrBackend(const QString& deviceAddress, QObject* parent)
    : RadioBackend(parent),
      m_device(new HiqSdrDevice(deviceAddress, this)),
      m_iqReceiver(new HiqSdrIqReceiver(deviceAddress, this)),
      m_spectrumDspWorker(new SpectrumDspWorker()),
      m_audioDspWorker(new AudioDspWorker()),
      m_deviceAddress(deviceAddress)
{
    m_spectrumDspWorker->setSampleRate(m_sampleRate);
    m_spectrumDspWorker->setPanBandwidthHz(m_panBandwidthHz);
    m_spectrumDspWorker->setCenterFrequencyHz(
        qRound64(m_panCenterFrequencyMhz * 1000000.0));
    m_spectrumDspWorker->moveToThread(&m_spectrumDspThread);
    connect(&m_spectrumDspThread, &QThread::started,
            m_spectrumDspWorker, &SpectrumDspWorker::start);
    connect(&m_spectrumDspThread, &QThread::finished,
            m_spectrumDspWorker, &QObject::deleteLater);
    m_spectrumDspThread.start();
    m_audioDspWorker->setInputSampleRate(m_sampleRate);
    m_audioDspWorker->setFilter(m_filterLowHz, m_filterHighHz);
    m_audioDspWorker->setAgcMode(m_agcMode);
    m_audioDspWorker->setAgcThreshold(m_agcThreshold);
    m_audioDspWorker->moveToThread(&m_audioDspThread);
    connect(&m_audioDspThread, &QThread::started,
            m_audioDspWorker, &AudioDspWorker::start);
    connect(&m_audioDspThread, &QThread::finished,
            m_audioDspWorker, &QObject::deleteLater);
    m_audioDspThread.start();
    connect(m_iqReceiver, &HiqSdrIqReceiver::iqDatagramReceived,
            this, &HiqSdrBackend::onIqDatagramReceived);
}

HiqSdrBackend::~HiqSdrBackend()
{
    if (m_started) {
        m_iqReceiver->stop();
        m_device->close();
    }
    m_audioDspThread.quit();
    m_audioDspThread.wait();
    m_spectrumDspThread.quit();
    m_spectrumDspThread.wait();
}

bool HiqSdrBackend::start()
{
    const long long frequencyHz = qRound64(m_panCenterFrequencyMhz * 1000000.0);
    if (!m_device->open(m_sampleRate, frequencyHz)) {
        return false;
    }
    m_started = true;
    if (!sendInitialControlState() || !m_iqReceiver->start()) {
        m_started = false;
        m_device->close();
        return false;
    }
    return true;
}

bool HiqSdrBackend::setSliceFrequencyMhz(double frequencyMhz)
{
    if (frequencyMhz <= 0.0) {
        return false;
    }
    m_sliceFrequencyMhz = frequencyMhz;
    m_audioDspWorker->setFrequencyOffsetHz(
        (m_sliceFrequencyMhz - m_panCenterFrequencyMhz) * 1000000.0);
    return true;
}

double HiqSdrBackend::sliceFrequencyMhz() const
{
    return m_sliceFrequencyMhz;
}

bool HiqSdrBackend::setSliceMode(const QString& mode)
{
    if (mode == QStringLiteral("USB")) {
        m_audioDspWorker->setSideband(SsbDemodulator::Sideband::Usb);
    } else if (mode == QStringLiteral("LSB")) {
        m_audioDspWorker->setSideband(SsbDemodulator::Sideband::Lsb);
    } else {
        return false;
    }
    m_sliceMode = mode;
    return true;
}

QString HiqSdrBackend::sliceMode() const
{
    return m_sliceMode;
}

bool HiqSdrBackend::setAgcMode(const QString& mode)
{
    if (mode != QStringLiteral("OFF") && mode != QStringLiteral("FAST")
        && mode != QStringLiteral("MED") && mode != QStringLiteral("SLOW")) {
        return false;
    }
    m_agcMode = mode;
    m_audioDspWorker->setAgcMode(m_agcMode);
    return true;
}

QString HiqSdrBackend::agcMode() const
{
    return m_agcMode;
}

bool HiqSdrBackend::setAgcThreshold(int threshold)
{
    if (threshold < 0 || threshold > 100) {
        return false;
    }
    m_agcThreshold = threshold;
    m_audioDspWorker->setAgcThreshold(m_agcThreshold);
    return true;
}

int HiqSdrBackend::agcThreshold() const
{
    return m_agcThreshold;
}

bool HiqSdrBackend::setPanCenterFrequencyMhz(double frequencyMhz)
{
    if (frequencyMhz <= 0.0) {
        return false;
    }
    if (!m_started) {
        m_panCenterFrequencyMhz = frequencyMhz;
        m_spectrumDspWorker->setCenterFrequencyHz(qRound64(frequencyMhz * 1000000.0));
        m_audioDspWorker->setFrequencyOffsetHz(
            (m_sliceFrequencyMhz - m_panCenterFrequencyMhz) * 1000000.0);
        return true;
    }
    if (!m_device->setFrequency(qRound64(frequencyMhz * 1000000.0))) {
        return false;
    }
    if (!m_device->setPreselector(preselectorForFrequency(frequencyMhz))) {
        return false;
    }
    m_panCenterFrequencyMhz = frequencyMhz;
    m_spectrumDspWorker->setCenterFrequencyHz(qRound64(frequencyMhz * 1000000.0));
    m_audioDspWorker->setFrequencyOffsetHz(
        (m_sliceFrequencyMhz - m_panCenterFrequencyMhz) * 1000000.0);
    return true;
}

double HiqSdrBackend::panCenterFrequencyMhz() const
{
    return m_panCenterFrequencyMhz;
}

bool HiqSdrBackend::setPanBandwidthHz(int bandwidthHz)
{
    if (bandwidthHz > kMaximumPanBandwidthHz) {
        qWarning() << "Requested pan bandwidth exceeds HiQSDR limit; using"
                   << kMaximumPanBandwidthHz << "Hz instead of" << bandwidthHz;
        bandwidthHz = kMaximumPanBandwidthHz;
    }
    const int sampleRate = sampleRateForPanBandwidth(bandwidthHz);
    if (sampleRate == 0) {
        return false;
    }
    if (sampleRate == m_sampleRate) {
        m_panBandwidthHz = bandwidthHz;
        m_spectrumDspWorker->setPanBandwidthHz(m_panBandwidthHz);
        return true;
    }

    const int previousSampleRate = m_sampleRate;
    if (m_started) {
        m_iqReceiver->stop();
        if (!m_device->setSampleRate(sampleRate)) {
            m_iqReceiver->start();
            return false;
        }
        if (!m_iqReceiver->start()) {
            m_device->setSampleRate(previousSampleRate);
            m_iqReceiver->start();
            return false;
        }
    }
    m_sampleRate = sampleRate;
    m_panBandwidthHz = bandwidthHz;
    m_spectrumDspWorker->setSampleRate(m_sampleRate);
    m_spectrumDspWorker->setPanBandwidthHz(m_panBandwidthHz);
    m_audioDspWorker->setInputSampleRate(m_sampleRate);
    qInfo() << "HiQSDR pan bandwidth:" << bandwidthHz
            << "sample rate:" << m_sampleRate;
    return true;
}

int HiqSdrBackend::panBandwidthHz() const
{
    return m_panBandwidthHz;
}

int HiqSdrBackend::spectrumSampleRateHz() const
{
    return m_sampleRate;
}

bool HiqSdrBackend::setSpectrumPointCount(int pointCount)
{
    m_spectrumDspWorker->setOutputPointCount(pointCount);
    return true;
}

void HiqSdrBackend::setSpectrumFrameRate(int framesPerSecond)
{
    m_spectrumDspWorker->setFrameRate(framesPerSecond);
}

bool HiqSdrBackend::setSpectrumDbmRange(float minimumDbm, float maximumDbm)
{
    if (minimumDbm >= maximumDbm) {
        return false;
    }
    m_spectrumDspWorker->setDbmRange(minimumDbm, maximumDbm);
    return true;
}

bool HiqSdrBackend::setFilter(int lowHz, int highHz)
{
    if (lowHz >= highHz) {
        return false;
    }
    // HiQSDR bandwidth is its IQ sample rate, not the receiver audio filter.
    m_filterLowHz = lowHz;
    m_filterHighHz = highHz;
    m_audioDspWorker->setFilter(m_filterLowHz, m_filterHighHz);
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
    if (input < 0) {
        return false;
    }
    if (!m_started) {
        m_antenna = antenna;
        return true;
    }
    if (!m_device->setAntennaInput(input)) {
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
    if (!m_started) {
        m_preampEnabled = enabled;
        return true;
    }
    if (!m_device->setPreampEnabled(enabled)) {
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
    if (attenuationDb < 0 || attenuationDb > 44) {
        return false;
    }
    if (!m_started) {
        m_attenuatorDb = attenuationDb;
        return true;
    }
    if (!m_device->setAttenuatorDb(attenuationDb)) {
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

bool HiqSdrBackend::hasSpectrumFrame() const
{
    return m_spectrumDspWorker->hasSpectrumFrame();
}

SpectrumFrame HiqSdrBackend::createSpectrumFrame()
{
    return m_spectrumDspWorker->createSpectrumFrame();
}

QVector<float> HiqSdrBackend::createUsbAudio(int frameCount)
{
    QVector<float> audio;
    if (!m_audioDspWorker->takeAudio(frameCount, &audio)) {
        return audio;
    }
    const float gain = m_audioMuted ? 0.0f : static_cast<float>(m_audioLevel) / 100.0f;
    for (float& sample : audio) {
        sample *= gain;
    }
    return audio;
}

float HiqSdrBackend::filterLevelDbm() const
{
    return -150.0f;
}

void HiqSdrBackend::onIqDatagramReceived(const QByteArray& datagram)
{
    ++m_iqDatagramsReceived;
    if (m_iqDatagramsReceived == 1) {
        qInfo() << "HiQSDR IQ stream is receiving 1442-byte datagrams";
    }
    m_audioDspWorker->enqueueIqDatagram(datagram);
    m_spectrumDspWorker->enqueueIqDatagram(datagram);
}

bool HiqSdrBackend::sendInitialControlState()
{
    return setPanCenterFrequencyMhz(m_panCenterFrequencyMhz)
        && setAntenna(m_antenna)
        && setPreampEnabled(m_preampEnabled)
        && setAttenuatorDb(m_attenuatorDb);
}

int HiqSdrBackend::sampleRateForPanBandwidth(int bandwidthHz) const
{
    if (bandwidthHz <= 40000) {
        return 96000;
    }
    if (bandwidthHz <= 80000) {
        return 192000;
    }
    if (bandwidthHz <= 200000) {
        return 240000;
    }
    if (bandwidthHz <= 280000) {
        return 320000;
    }
    if (bandwidthHz <= 400000) {
        return 480000;
    }
    if (bandwidthHz <= 800000) {
        return 960000;
    }
    if (bandwidthHz <= 960000) {
        return 960000;
    }
    return 0;
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
