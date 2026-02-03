#include <QCoreApplication>
#include <QDebug>

#include "daemon/khronicle_daemon.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("khronicle-daemon"));
    qInfo() << "Khronicle daemon starting...";

    khronicle::KhronicleDaemon daemon;
    daemon.start();

    return app.exec();
}
