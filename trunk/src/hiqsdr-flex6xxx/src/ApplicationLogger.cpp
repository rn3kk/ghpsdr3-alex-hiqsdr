#include "ApplicationLogger.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

#include <cstdio>

namespace {
QFile* logFile = nullptr;
QMutex logMutex;

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARNING");
    case QtCriticalMsg:
        return QStringLiteral("CRITICAL");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}
}

bool ApplicationLogger::initialize(const QString& filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.dir().mkpath(QStringLiteral("."))) {
        std::fprintf(stderr, "Cannot create log directory: %s\n",
                     qPrintable(fileInfo.dir().absolutePath()));
        return false;
    }

    logFile = new QFile(filePath);
    if (!logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        std::fprintf(stderr, "Cannot open log file: %s\n", qPrintable(filePath));
        delete logFile;
        logFile = nullptr;
        return false;
    }

    qInstallMessageHandler(ApplicationLogger::messageHandler);
    qInfo() << "Logging to" << logFile->fileName();
    return true;
}

void ApplicationLogger::shutdown()
{
    QMutexLocker locker(&logMutex);
    if (!logFile) {
        return;
    }
    logFile->close();
    delete logFile;
    logFile = nullptr;
    qInstallMessageHandler(nullptr);
}

void ApplicationLogger::messageHandler(QtMsgType type, const QMessageLogContext& context,
                                       const QString& message)
{
    Q_UNUSED(context)
    const QString line = QStringLiteral("%1 [%2] %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(type), message);
    QMutexLocker locker(&logMutex);
    if (logFile) {
        logFile->write(line.toUtf8());
        logFile->flush();
    }
    std::fputs(qPrintable(line), stderr);
    std::fflush(stderr);
    if (type == QtFatalMsg) {
        std::abort();
    }
}
