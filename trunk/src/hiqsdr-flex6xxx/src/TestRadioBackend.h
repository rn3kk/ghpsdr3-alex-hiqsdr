#pragma once

#include "RadioBackend.h"
#include "TestSignalProcessor.h"

class TestRadioBackend : public RadioBackend
{
    Q_OBJECT

public:
    explicit TestRadioBackend(QObject* parent = nullptr);

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

    SpectrumFrame createSpectrumFrame() override;
    QVector<float> createUsbAudio(int frameCount) override;
    float filterLevelDbm() const override;

private:
    TestSignalProcessor m_signalProcessor;
    double m_sliceFrequencyMhz{14.100000};
    QString m_sliceMode{QStringLiteral("USB")};
    QString m_agcMode{QStringLiteral("MED")};
    int m_agcThreshold{50};
    double m_panCenterFrequencyMhz{14.100000};
    int m_panBandwidthHz{200000};
    int m_filterLowHz{100};
    int m_filterHighHz{2900};
    QString m_antenna{QStringLiteral("ANT1")};
    bool m_preampEnabled{false};
    int m_attenuatorDb{0};
    int m_audioLevel{50};
    bool m_audioMuted{false};
};
