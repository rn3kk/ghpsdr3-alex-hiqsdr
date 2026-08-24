#pragma once

#include <QString>
#include <QtGlobal>

class ApplicationLogger
{
public:
    static bool initialize(const QString& filePath);
    static void shutdown();

private:
    static void messageHandler(QtMsgType type, const QMessageLogContext& context,
                               const QString& message);
};
