#pragma once

#include <QByteArray>
#include <QMutex>
#include <QQueue>
#include <QObject>

#include "SsbDemodulator.h"

class QTimer;

class AudioDspWorker : public QObject
{
    Q_OBJECT

public:
    AudioDspWorker();

    void enqueueIqDatagram(const QByteArray& datagram);
    void setInputSampleRate(int sampleRate);
    void setFrequencyOffsetHz(double offsetHz);
    void setFilter(int lowHz, int highHz);
    void setSideband(SsbDemodulator::Sideband sideband);
    void setAgcMode(const QString& mode);
    void setAgcThreshold(int threshold);
    bool takeAudio(int frameCount, QVector<float>* audio);

public slots:
    void start();

private slots:
    void processPendingIq();

private:
    static constexpr int MaximumQueuedDatagrams = 4096;
    static constexpr int MaximumAudioBufferSamples = SsbDemodulator::OutputSampleRate * 2;

    void appendAudio(const QVector<float>& audio);

    QMutex m_inputMutex;
    QMutex m_dspMutex;
    QMutex m_audioMutex;
    QQueue<QByteArray> m_pendingDatagrams;
    QVector<float> m_audioBuffer;
    int m_audioReadIndex{0};
    int m_audioWriteIndex{0};
    int m_audioSampleCount{0};
    SsbDemodulator m_demodulator;
    QTimer* m_processTimer;
};
