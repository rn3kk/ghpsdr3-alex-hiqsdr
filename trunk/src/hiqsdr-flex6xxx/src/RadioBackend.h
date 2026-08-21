#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "RadioTypes.h"

class RadioBackend : public QObject
{
    Q_OBJECT

public:
    explicit RadioBackend(QObject* parent = nullptr);

    virtual bool start() = 0;
    virtual bool setSliceFrequencyMhz(double frequencyMhz) = 0;
    virtual double sliceFrequencyMhz() const = 0;
    virtual bool setFilter(int lowHz, int highHz) = 0;
    virtual int filterLowHz() const = 0;
    virtual int filterHighHz() const = 0;
    virtual bool setAntenna(const QString& antenna) = 0;
    virtual QString antenna() const = 0;
    virtual bool setPreampEnabled(bool enabled) = 0;
    virtual bool preampEnabled() const = 0;
    virtual bool setAttenuatorDb(int attenuationDb) = 0;
    virtual int attenuatorDb() const = 0;
    virtual bool setAudioLevel(int level) = 0;
    virtual int audioLevel() const = 0;
    virtual void setAudioMuted(bool muted) = 0;
    virtual bool audioMuted() const = 0;

    virtual SpectrumFrame createSpectrumFrame() = 0;
    virtual QVector<float> createUsbAudio(int frameCount) = 0;
    virtual float filterLevelDbm() const = 0;
};
