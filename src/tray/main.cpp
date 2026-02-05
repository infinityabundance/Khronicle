#include <QApplication>
#include <QDebug>
#include <QIcon>
#include <QSystemTrayIcon>

#include "tray/KhronicleTray.hpp"
#include "common/process_utils.hpp"
#include "common/logging.hpp"

#include <nlohmann/json.hpp>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        qWarning() << "System tray not available. Exiting.";
        return 1;
    }

    app.setQuitOnLastWindowClosed(false);

    bool codexTrace = qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--codex-trace")) {
            codexTrace = true;
        }
    }
    khronicle::logging::initLogging(QStringLiteral("khronicle-tray"), codexTrace);
    KLOG_INFO(QStringLiteral("main"),
              QStringLiteral("main"),
              QStringLiteral("tray_start"),
              QStringLiteral("user_start"),
              QStringLiteral("qt_app"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());

    const QString iconPath = khronicle::appIconPath();
    if (!iconPath.isEmpty()) {
        app.setWindowIcon(QIcon(iconPath));
    }

    // Tray runs as a background UI with periodic daemon queries.
    KhronicleTray tray;

    return app.exec();
}
