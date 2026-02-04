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

    // Tray runs as a background UI with periodic daemon queries.
    KhronicleTray tray;

    return app.exec();
}
