#pragma once

#include <QByteArray>
#include <QThread>

#include <memory>

#include "RadioBackend.h"

class HiqSdrBackend : public RadioBackend
{
    Q_OBJECT

public:
    explicit HiqSdrBackend(const QString& deviceAddress, QObject* parent = nullptr);
    ~HiqSdrBackend() override;

    bool start() override;
    bool setSliceFrequencyMhz(double frequencyMhz) override;
    double sliceFrequencyMhz() const override;
    bool setSliceMode(const QString& mode) override;
    QString sliceMode() const override;
    bool setAgcMode(const QString& mode) override;
    QString agcMode() const override;
    bool setAgcThreshold(int threshold) override;
    int agcThreshold() const override;
    bool setPanCenterFrequencyMhz(double frequencyMhz) override;
    double panCenterFrequencyMhz() const override;
    bool setPanBandwidthHz(int bandwidthHz) override;
    int panBandwidthHz() const override;
    int spectrumSampleRateHz() const override;
    bool setSpectrumPointCount(int pointCount) override;
    void setSpectrumFrameRate(int framesPerSecond) override;
    bool setSpectrumDbmRange(float minimumDbm, float maximumDbm) override;
    bool setFilter(int lowHz, int highHz) override;
    int filterLowHz() const override;
    int filterHighHz() const override;
    bool setAntenna(const QString& antenna) override;
    QString antenna() const override;
    bool setPreampEnabled(bool enabled) override;
    bool preampEnabled() const override;
    bool setAttenuatorDb(int attenuationDb) override;
    int attenuatorDb() const override;
    bool setAudioLevel(int level) override;
    int audioLevel() const override;
    void setAudioMuted(bool muted) override;
    bool audioMuted() const override;

    bool hasSpectrumFrame() const override;
    SpectrumFrame createSpectrumFrame() override;
    QVector<float> createUsbAudio(int frameCount) override;
    float filterLevelDbm() const override;

private slots:
    void onIqDatagramReceived(const QByteArray& datagram);

private:
    bool sendInitialControlState();
    int sampleRateForPanBandwidth(int bandwidthHz) const;
    int preselectorForFrequency(double frequencyMhz) const;

    class HiqSdrDevice* m_device;
    class HiqSdrIqReceiver* m_iqReceiver;
    class SpectrumDspWorker* m_spectrumDspWorker;
    QThread m_spectrumDspThread;
    class AudioDspWorker* m_audioDspWorker;
    QThread m_audioDspThread;
    QString m_deviceAddress;
    bool m_started{false};
    double m_sliceFrequencyMhz{14.100000};
    QString m_sliceMode{QStringLiteral("USB")};
    QString m_agcMode{QStringLiteral("MED")};
    int m_agcThreshold{50};
    double m_panCenterFrequencyMhz{14.100000};
    int m_panBandwidthHz{200000};
    int m_sampleRate{240000};
    int m_filterLowHz{100};
    int m_filterHighHz{2900};
    QString m_antenna{QStringLiteral("ANT1")};
    bool m_preampEnabled{false};
    int m_attenuatorDb{0};
    int m_audioLevel{50};
    bool m_audioMuted{false};
    quint64 m_iqDatagramsReceived{0};
};
