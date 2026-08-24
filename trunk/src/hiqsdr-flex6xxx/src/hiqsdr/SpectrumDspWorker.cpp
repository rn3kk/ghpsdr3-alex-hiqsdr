#include "SpectrumDspWorker.h"

#include <QTimer>

SpectrumDspWorker::SpectrumDspWorker()
    : m_processTimer(new QTimer(this))
{
    m_processTimer->setInterval(1);
    connect(m_processTimer, &QTimer::timeout, this, &SpectrumDspWorker::processPendingIq);
}

void SpectrumDspWorker::enqueueIqDatagram(const QByteArray& datagram)
{
    QMutexLocker locker(&m_mutex);
    while (m_pendingDatagrams.size() >= MaximumQueuedDatagrams) {
        m_pendingDatagrams.dequeue();
    }
    m_pendingDatagrams.enqueue(datagram);
}

void SpectrumDspWorker::setSampleRate(int sampleRate)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setSampleRate(sampleRate);
}

void SpectrumDspWorker::setPanBandwidthHz(int bandwidthHz)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setPanBandwidthHz(bandwidthHz);
}

void SpectrumDspWorker::setOutputPointCount(int pointCount)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setOutputPointCount(pointCount);
}

void SpectrumDspWorker::setFrameRate(int framesPerSecond)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setFrameRate(framesPerSecond);
}

void SpectrumDspWorker::setCenterFrequencyHz(qint64 frequencyHz)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setCenterFrequencyHz(frequencyHz);
}

void SpectrumDspWorker::setDbmRange(float minimumDbm, float maximumDbm)
{
    QMutexLocker locker(&m_mutex);
    m_spectrumProcessor.setDbmRange(minimumDbm, maximumDbm);
}

bool SpectrumDspWorker::hasSpectrumFrame() const
{
    QMutexLocker locker(&m_mutex);
    return m_spectrumProcessor.hasSpectrumFrame();
}

SpectrumFrame SpectrumDspWorker::createSpectrumFrame()
{
    QMutexLocker locker(&m_mutex);
    return m_spectrumProcessor.createSpectrumFrame();
}

void SpectrumDspWorker::start()
{
    m_processTimer->start();
}

void SpectrumDspWorker::processPendingIq()
{
    QMutexLocker locker(&m_mutex);
    while (!m_pendingDatagrams.isEmpty()) {
        m_spectrumProcessor.addIqDatagram(m_pendingDatagrams.dequeue());
    }
}
