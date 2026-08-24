#pragma once

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QQueue>

#include "HiqSdrSpectrumProcessor.h"

class QTimer;

class SpectrumDspWorker : public QObject
{
    Q_OBJECT

public:
    SpectrumDspWorker();

    void enqueueIqDatagram(const QByteArray& datagram);
    void setSampleRate(int sampleRate);
    void setPanBandwidthHz(int bandwidthHz);
    void setOutputPointCount(int pointCount);
    void setFrameRate(int framesPerSecond);
    void setCenterFrequencyHz(qint64 frequencyHz);
    void setDbmRange(float minimumDbm, float maximumDbm);
    bool hasSpectrumFrame() const;
    SpectrumFrame createSpectrumFrame();

public slots:
    void start();

private slots:
    void processPendingIq();

private:
    static constexpr int MaximumQueuedDatagrams = 32;

    mutable QMutex m_mutex;
    QQueue<QByteArray> m_pendingDatagrams;
    HiqSdrSpectrumProcessor m_spectrumProcessor;
    QTimer* m_processTimer;
};
