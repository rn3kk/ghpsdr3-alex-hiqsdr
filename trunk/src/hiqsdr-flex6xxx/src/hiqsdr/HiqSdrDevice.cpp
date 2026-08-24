#include "HiqSdrDevice.h"

#include <QByteArray>
#include <QDebug>
#include <QHostAddress>
#include <QUdpSocket>
#include <QtEndian>

namespace {
constexpr quint16 kControlPort = 48248;
constexpr int kControlResponseTimeoutMs = 1000;
constexpr double kReferenceClockHz = 122880000.0;
constexpr double kPhaseScale = 4294967296.0;

#pragma pack(push, 1)
struct ControlPacket
{
    quint8 startS;
    quint8 startT;
    quint32 receivePhase;
    quint32 transmitPhase;
    quint8 transmitLevel;
    quint8 transmitControl;
    quint8 receiveControl;
    quint8 firmwareVersion;
    quint8 x1Pins;
    quint8 attenuatorPins;
    quint8 antennaPins;
    quint8 reserved[5];
};
#pragma pack(pop)

static_assert(sizeof(ControlPacket) == 22);
}

HiqSdrDevice::HiqSdrDevice(const QString& address, QObject* parent)
    : QObject(parent),
      m_controlSocket(new QUdpSocket(this)),
      m_address(address)
{
}

bool HiqSdrDevice::open(int sampleRate, qint64 frequencyHz)
{
    if (m_isOpen) {
        return true;
    }
    if (decimationCode(sampleRate) == 0) {
        qWarning() << "Unsupported HiQSDR sample rate:" << sampleRate;
        return false;
    }
    if (QHostAddress(m_address).isNull()) {
        qWarning() << "Invalid HiQSDR address:" << m_address;
        return false;
    }

    m_sampleRate = sampleRate;
    m_frequencyHz = frequencyHz;
    m_controlSocket->connectToHost(m_address, kControlPort);
    if (!m_controlSocket->waitForConnected(kControlResponseTimeoutMs)) {
        qWarning() << "Cannot connect to HiQSDR control port" << m_address
                   << kControlPort << m_controlSocket->errorString();
        close();
        return false;
    }
    if (!ping()) {
        qWarning() << "HiQSDR did not confirm the control packet from" << m_address;
        close();
        return false;
    }

    m_isOpen = true;
    if (!sendControlPacket()) {
        qWarning() << "Cannot send initial HiQSDR control state to" << m_address;
        close();
        return false;
    }
    qInfo() << "HiQSDR control connected:" << m_address
            << "sample rate:" << m_sampleRate;
    return true;
}

void HiqSdrDevice::close()
{
    m_controlSocket->disconnectFromHost();
    m_controlSocket->close();
    m_isOpen = false;
}

bool HiqSdrDevice::setFrequency(qint64 frequencyHz)
{
    if (!m_isOpen || frequencyHz <= 0) {
        return false;
    }
    const qint64 previousFrequency = m_frequencyHz;
    m_frequencyHz = frequencyHz;
    if (sendControlPacket()) {
        return true;
    }
    m_frequencyHz = previousFrequency;
    return false;
}

bool HiqSdrDevice::setSampleRate(int sampleRate)
{
    if (!m_isOpen || decimationCode(sampleRate) == 0) {
        return false;
    }
    const int previousSampleRate = m_sampleRate;
    m_sampleRate = sampleRate;
    if (sendControlPacket()) {
        return true;
    }
    m_sampleRate = previousSampleRate;
    return false;
}

bool HiqSdrDevice::setPreselector(int pins)
{
    if (!m_isOpen || pins < 0 || pins > 15) {
        return false;
    }
    const int previousPins = m_preselector;
    m_preselector = pins;
    if (sendControlPacket()) {
        return true;
    }
    m_preselector = previousPins;
    return false;
}

bool HiqSdrDevice::setPreampEnabled(bool enabled)
{
    if (!m_isOpen) {
        return false;
    }
    const bool previousEnabled = m_preampEnabled;
    m_preampEnabled = enabled;
    if (sendControlPacket()) {
        return true;
    }
    m_preampEnabled = previousEnabled;
    return false;
}

bool HiqSdrDevice::setAttenuatorDb(int attenuationDb)
{
    if (!m_isOpen || attenuationDb < 0 || attenuationDb > 44) {
        return false;
    }
    const int previousAttenuation = m_attenuatorDb;
    m_attenuatorDb = attenuationDb;
    if (sendControlPacket()) {
        return true;
    }
    m_attenuatorDb = previousAttenuation;
    return false;
}

bool HiqSdrDevice::setAntennaInput(int input)
{
    if (!m_isOpen || input < 0 || input > 1) {
        return false;
    }
    const int previousInput = m_antennaInput;
    m_antennaInput = input;
    if (sendControlPacket()) {
        return true;
    }
    m_antennaInput = previousInput;
    return false;
}

bool HiqSdrDevice::ping()
{
    ControlPacket packet{};
    packet.startS = 'S';
    packet.startT = 't';
    packet.receivePhase = qToLittleEndian(tuningPhase(m_frequencyHz));
    packet.transmitPhase = qToLittleEndian(tuningPhase(m_frequencyHz));
    packet.transmitControl = 0x02;
    packet.receiveControl = decimationCode(m_sampleRate) - 1;
    packet.firmwareVersion = 0;

    const QByteArray data(reinterpret_cast<const char*>(&packet), sizeof(packet));
    if (!sendPacket(data)) {
        qWarning() << "Cannot send HiQSDR probe:" << m_controlSocket->errorString();
        return false;
    }
    if (!m_controlSocket->waitForReadyRead(kControlResponseTimeoutMs)) {
        qWarning() << "HiQSDR control response timeout:" << m_controlSocket->errorString();
        return false;
    }
    const qint64 bytes = m_controlSocket->pendingDatagramSize();
    if (bytes < 14 || bytes > static_cast<qint64>(sizeof(ControlPacket))) {
        qWarning() << "Unexpected HiQSDR control response length:" << bytes;
        return false;
    }
    QByteArray response;
    response.resize(static_cast<int>(bytes));
    const qint64 received = m_controlSocket->readDatagram(response.data(), response.size());
    if (received != bytes) {
        qWarning() << "Cannot read HiQSDR control response:" << m_controlSocket->errorString();
        return false;
    }
    qInfo() << "HiQSDR control response received:" << bytes << "bytes";
    return true;
}

bool HiqSdrDevice::sendControlPacket()
{
    ControlPacket packet{};
    packet.startS = 'S';
    packet.startT = 't';
    packet.receivePhase = qToLittleEndian(tuningPhase(m_frequencyHz));
    packet.transmitPhase = qToLittleEndian(tuningPhase(m_frequencyHz));
    packet.transmitControl = 0x06;
    packet.receiveControl = decimationCode(m_sampleRate) - 1;
    packet.firmwareVersion = 3;
    packet.x1Pins = static_cast<quint8>(m_preselector & 0x0F);
    if (m_preampEnabled) {
        packet.x1Pins |= 0x10;
    }
    packet.attenuatorPins = attenuatorPins(m_attenuatorDb);
    packet.antennaPins = m_antennaInput == 0 ? 0x00 : 0x01;

    const QByteArray data(reinterpret_cast<const char*>(&packet), sizeof(packet));
    return sendPacket(data);
}

bool HiqSdrDevice::sendPacket(const QByteArray& packet)
{
    const qint64 written = m_controlSocket->write(packet);
    if (written != packet.size()) {
        qWarning() << "Cannot queue HiQSDR UDP packet:" << m_controlSocket->errorString();
        return false;
    }
    // QUdpSocket accepts a complete datagram synchronously. Waiting for a
    // bytesWritten signal here can fail although the packet was queued.
    return true;
}

quint32 HiqSdrDevice::tuningPhase(qint64 frequencyHz) const
{
    return static_cast<quint32>(static_cast<double>(frequencyHz)
                                / kReferenceClockHz * kPhaseScale + 0.5);
}

quint8 HiqSdrDevice::decimationCode(int sampleRate) const
{
    switch (sampleRate) {
    case 1920000: return 0x01;
    case 960000: return 0x02;
    case 480000: return 0x04;
    case 320000: return 0x06;
    case 240000: return 0x08;
    case 192000: return 0x0A;
    case 160000: return 0x0C;
    case 120000: return 0x0F;
    case 96000: return 0x14;
    case 80000: return 0x18;
    case 64000: return 0x1E;
    case 60000: return 0x20;
    case 48000: return 0x28;
    default: return 0;
    }
}

quint8 HiqSdrDevice::attenuatorPins(int attenuationDb) const
{
    quint8 pins = 0;
    if (attenuationDb >= 20) {
        pins |= 0x10;
        attenuationDb -= 20;
    }
    if (attenuationDb >= 10) {
        pins |= 0x08;
        attenuationDb -= 10;
    }
    if (attenuationDb >= 8) {
        pins |= 0x04;
        attenuationDb -= 8;
    }
    if (attenuationDb >= 4) {
        pins |= 0x02;
        attenuationDb -= 4;
    }
    if (attenuationDb >= 2) {
        pins |= 0x01;
    }
    return pins;
}
