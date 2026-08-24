#include "AudioDspWorker.h"

#include <QTimer>

AudioDspWorker::AudioDspWorker()
    : m_audioBuffer(MaximumAudioBufferSamples),
      m_processTimer(new QTimer(this))
{
    m_processTimer->setInterval(1);
    connect(m_processTimer, &QTimer::timeout, this, &AudioDspWorker::processPendingIq);
}

void AudioDspWorker::enqueueIqDatagram(const QByteArray& datagram)
{
    QMutexLocker locker(&m_inputMutex);
    while (m_pendingDatagrams.size() >= MaximumQueuedDatagrams) {
        m_pendingDatagrams.dequeue();
    }
    m_pendingDatagrams.enqueue(datagram);
}

void AudioDspWorker::setInputSampleRate(int sampleRate)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setInputSampleRate(sampleRate);
}

void AudioDspWorker::setFrequencyOffsetHz(double offsetHz)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setFrequencyOffsetHz(offsetHz);
}

void AudioDspWorker::setFilter(int lowHz, int highHz)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setFilter(lowHz, highHz);
}

void AudioDspWorker::setSideband(SsbDemodulator::Sideband sideband)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setSideband(sideband);
}

void AudioDspWorker::setAgcMode(const QString& mode)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setAgcMode(mode);
}

void AudioDspWorker::setAgcThreshold(int threshold)
{
    QMutexLocker locker(&m_dspMutex);
    m_demodulator.setAgcThreshold(threshold);
}

bool AudioDspWorker::takeAudio(int frameCount, QVector<float>* audio)
{
    QMutexLocker locker(&m_audioMutex);
    if (audio == nullptr || frameCount <= 0 || m_audioSampleCount < frameCount) {
        return false;
    }

    audio->resize(frameCount);
    for (int index = 0; index < frameCount; ++index) {
        audio->operator[](index) = m_audioBuffer.at(m_audioReadIndex);
        m_audioReadIndex = (m_audioReadIndex + 1) % MaximumAudioBufferSamples;
    }
    m_audioSampleCount -= frameCount;
    return true;
}

void AudioDspWorker::start()
{
    m_processTimer->start();
}

void AudioDspWorker::processPendingIq()
{
    QQueue<QByteArray> datagrams;
    {
        QMutexLocker locker(&m_inputMutex);
        constexpr int maximumDatagramsPerPass = 64;
        while (!m_pendingDatagrams.isEmpty() && datagrams.size() < maximumDatagramsPerPass) {
            datagrams.enqueue(m_pendingDatagrams.dequeue());
        }
    }

    if (datagrams.isEmpty()) {
        return;
    }

    QVector<float> demodulatedAudio;
    {
        QMutexLocker locker(&m_dspMutex);
        while (!datagrams.isEmpty()) {
            m_demodulator.addIqDatagram(datagrams.dequeue());
        }
        demodulatedAudio = m_demodulator.takeAvailableAudio();
    }

    appendAudio(demodulatedAudio);
}

void AudioDspWorker::appendAudio(const QVector<float>& audio)
{
    if (audio.isEmpty()) {
        return;
    }
    QMutexLocker locker(&m_audioMutex);
    for (float sample : audio) {
        if (m_audioSampleCount >= MaximumAudioBufferSamples) {
            // Keep the newest continuous audio if the client has stopped reading it.
            m_audioReadIndex = (m_audioReadIndex + 1) % MaximumAudioBufferSamples;
            --m_audioSampleCount;
        }
        m_audioBuffer[m_audioWriteIndex] = sample;
        m_audioWriteIndex = (m_audioWriteIndex + 1) % MaximumAudioBufferSamples;
        ++m_audioSampleCount;
    }
}
