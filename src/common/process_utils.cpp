#include "common/process_utils.hpp"

#include <QCoreApplication>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

#include <unistd.h>

#include "common/logging.hpp"
#include <nlohmann/json.hpp>

namespace khronicle {

QString daemonSocketPath()
{
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        return runtimeDir + QStringLiteral("/khronicle.sock");
    }
    return QStringLiteral("/run/user/%1/khronicle.sock").arg(getuid());
}

bool isDaemonRunning()
{
    QLocalSocket socket;
    socket.connectToServer(daemonSocketPath());
    if (socket.waitForConnected(200)) {
        socket.disconnectFromServer();
        return true;
    }
    return false;
}

bool startDaemon()
{
    KLOG_INFO(QStringLiteral("ProcessUtils"),
              QStringLiteral("startDaemon"),
              QStringLiteral("start_daemon"),
              QStringLiteral("user_action"),
              QStringLiteral("systemd_or_fallback"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());

    const QString systemctl = QStringLiteral("systemctl");
    QStringList args = {QStringLiteral("--user"), QStringLiteral("start"),
                        QStringLiteral("khronicle-daemon.service")};
    if (QProcess::startDetached(systemctl, args)) {
        return true;
    }

    return QProcess::startDetached(QStringLiteral("khronicle-daemon"));
}

bool stopDaemon()
{
    KLOG_INFO(QStringLiteral("ProcessUtils"),
              QStringLiteral("stopDaemon"),
              QStringLiteral("stop_daemon"),
              QStringLiteral("user_action"),
              QStringLiteral("systemd_or_fallback"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());

    const QString systemctl = QStringLiteral("systemctl");
    QStringList args = {QStringLiteral("--user"), QStringLiteral("stop"),
                        QStringLiteral("khronicle-daemon.service")};
    if (QProcess::startDetached(systemctl, args)) {
        return true;
    }

    // Best-effort fallback: no direct shutdown RPC exists yet.
    return false;
}

bool isTrayRunning()
{
    QProcess proc;
    proc.start(QStringLiteral("pgrep"), {QStringLiteral("-x"),
                                         QStringLiteral("khronicle-tray")});
    if (!proc.waitForFinished(200)) {
        return false;
    }
    return proc.exitCode() == 0;
}

bool startTray()
{
    KLOG_INFO(QStringLiteral("ProcessUtils"),
              QStringLiteral("startTray"),
              QStringLiteral("start_tray"),
              QStringLiteral("user_action"),
              QStringLiteral("process_start"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());
    return QProcess::startDetached(QStringLiteral("khronicle-tray"));
}

} // namespace khronicle
