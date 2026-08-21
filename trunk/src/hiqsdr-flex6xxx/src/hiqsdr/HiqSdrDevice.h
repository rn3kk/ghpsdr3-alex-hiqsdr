#pragma once

#include <QObject>
#include <QString>
#include <QtTypes>

class QUdpSocket;

class HiqSdrDevice : public QObject
{
    Q_OBJECT

public:
    explicit HiqSdrDevice(const QString& address, QObject* parent = nullptr);

    bool open(int sampleRate, qint64 frequencyHz);
    void close();
    bool setFrequency(qint64 frequencyHz);
    bool setSampleRate(int sampleRate);
    bool setPreselector(int pins);
    bool setPreampEnabled(bool enabled);
    bool setAttenuatorDb(int attenuationDb);
    bool setAntennaInput(int input);

private:
    bool ping();
    bool sendControlPacket();
    bool sendPacket(const QByteArray& packet);
    quint32 tuningPhase(qint64 frequencyHz) const;
    quint8 decimationCode(int sampleRate) const;
    quint8 attenuatorPins(int attenuationDb) const;

    QUdpSocket* m_controlSocket;
    QString m_address;
    qint64 m_frequencyHz{14100000};
    int m_sampleRate{192000};
    int m_preselector{0};
    bool m_preampEnabled{false};
    int m_attenuatorDb{0};
    int m_antennaInput{0};
    bool m_isOpen{false};
};
