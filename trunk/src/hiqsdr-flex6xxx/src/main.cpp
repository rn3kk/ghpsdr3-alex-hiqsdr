#include <QCoreApplication>
#include <QCommandLineParser>
#include <QHostAddress>

#include "Flex8400Emulator.h"

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("hiqsdr-flex6xxx"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("HiQSDR to FLEX-8400 gateway"));
    parser.addHelpOption();
    parser.addOption({QStringLiteral("address"),
                      QStringLiteral("TCP address to listen on."),
                      QStringLiteral("address"),
                      QStringLiteral("0.0.0.0")});
    parser.addOption({QStringLiteral("port"),
                      QStringLiteral("SmartSDR TCP port."),
                      QStringLiteral("port"),
                      QStringLiteral("4992")});
    parser.process(application);

    bool portIsValid = false;
    const quint16 port = parser.value(QStringLiteral("port")).toUShort(&portIsValid);
    const QHostAddress address(parser.value(QStringLiteral("address")));
    if (!portIsValid || address.isNull()) {
        return 1;
    }

    Flex8400Emulator emulator;
    if (!emulator.start(address, port)) {
        return 1;
    }

    return application.exec();
}
