#pragma once

#include <QObject>
#include <QString>

class QUdpSocket;

class HiqSdrIqReceiver : public QObject
{
    Q_OBJECT

public:
    explicit HiqSdrIqReceiver(const QString& address, QObject* parent = nullptr);

    bool start();
    void stop();
    bool isRunning() const;

signals:
    void iqDatagramReceived(const QByteArray& datagram);

private slots:
    void onReadyRead();

private:
    bool sendStreamCommand(quint8 command);

    QUdpSocket* m_socket;
    QString m_address;
    bool m_running{false};
    bool m_reportedInvalidDatagram{false};
};
