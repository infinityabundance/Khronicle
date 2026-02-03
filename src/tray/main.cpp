#include <QApplication>
#include <QSystemTrayIcon>
#include <QDebug>

#include "tray/KhronicleTray.hpp"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "System tray not available. Exiting.";
        return 1;
    }

    app.setQuitOnLastWindowClosed(false);

    KhronicleTray tray;

    return app.exec();
}
