#include "common/process_utils.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>

#include <unistd.h>

#include "common/logging.hpp"
#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

QString findSiblingBinary(const QString &name)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList relCandidates = {
        QStringLiteral("."),
        QStringLiteral(".."),
        QStringLiteral("../.."),
        QStringLiteral("../src/daemon"),
        QStringLiteral("../src/tray"),
        QStringLiteral("../../src/daemon"),
        QStringLiteral("../../src/tray"),
        QStringLiteral("../bin"),
        QStringLiteral("../../bin"),
    };

    for (const QString &relPath : relCandidates) {
        const QString candidate =
            QDir(appDir).absoluteFilePath(relPath + QDir::separator() + name);
        QFileInfo info(candidate);
        if (info.exists() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

bool isDevBuildTree()
{
    QString dir = QCoreApplication::applicationDirPath();
    for (int i = 0; i < 4; ++i) {
        const QString cachePath =
            QDir(dir).absoluteFilePath(QStringLiteral("CMakeCache.txt"));
        if (QFileInfo::exists(cachePath)) {
            return true;
        }
        QDir parent(dir);
        if (!parent.cdUp()) {
            break;
        }
        dir = parent.absolutePath();
    }
    return false;
}

} // namespace

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

    // Try sibling binary first (for dev builds)
    const QString siblingDaemon = findSiblingBinary(QStringLiteral("khronicle-daemon"));
    if (!siblingDaemon.isEmpty()) {
        if (QProcess::startDetached(siblingDaemon, {})) {
            return true;
        }
    }

    const bool devTree = isDevBuildTree();

    // Try systemctl (for installed systems)
    if (!devTree) {
        const QString systemctl = QStringLiteral("systemctl");
        QStringList args = {QStringLiteral("--user"), QStringLiteral("start"),
                            QStringLiteral("khronicle-daemon.service")};
        if (QProcess::startDetached(systemctl, args)) {
            return true;
        }
    }

    // Fall back to PATH lookup
    return QProcess::startDetached(QStringLiteral("khronicle-daemon"), {});
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

    if (!isDevBuildTree()) {
        const QString systemctl = QStringLiteral("systemctl");
        QStringList args = {QStringLiteral("--user"), QStringLiteral("stop"),
                            QStringLiteral("khronicle-daemon.service")};
        if (QProcess::startDetached(systemctl, args)) {
            return true;
        }
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

    // Try sibling binary first (for dev builds)
    const QString siblingTray = findSiblingBinary(QStringLiteral("khronicle-tray"));
    if (!siblingTray.isEmpty()) {
        if (QProcess::startDetached(siblingTray, {})) {
            return true;
        }
    }

    // Fall back to PATH lookup
    return QProcess::startDetached(QStringLiteral("khronicle-tray"), {});
}

bool startUi()
{
    KLOG_INFO(QStringLiteral("ProcessUtils"),
              QStringLiteral("startUi"),
              QStringLiteral("start_ui"),
              QStringLiteral("user_action"),
              QStringLiteral("process_start"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());

    const QString siblingUi = findSiblingBinary(QStringLiteral("khronicle"));
    if (!siblingUi.isEmpty()) {
        if (QProcess::startDetached(siblingUi, {})) {
            return true;
        }
    }

    return QProcess::startDetached(QStringLiteral("khronicle"), {});
}

} // namespace khronicle
