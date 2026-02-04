#include "ui/backend/DaemonController.hpp"

#include "common/logging.hpp"
#include "common/process_utils.hpp"

#include <nlohmann/json.hpp>

namespace khronicle {

DaemonController::DaemonController(QObject *parent)
    : QObject(parent)
{
    refreshDaemonStatus();
}

bool DaemonController::daemonRunning() const
{
    return m_daemonRunning;
}

void DaemonController::refreshDaemonStatus()
{
    const bool running = isDaemonRunning();
    if (running != m_daemonRunning) {
        m_daemonRunning = running;
        emit daemonRunningChanged();
    }
}

bool DaemonController::startDaemonFromUi()
{
    const bool result = startDaemon();
    refreshDaemonStatus();
    KLOG_INFO(QStringLiteral("DaemonController"),
              QStringLiteral("startDaemonFromUi"),
              QStringLiteral("start_daemon"),
              QStringLiteral("user_action"),
              QStringLiteral("systemd_or_fallback"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"success", result}});
    return result;
}

bool DaemonController::stopDaemonFromUi()
{
    const bool result = stopDaemon();
    refreshDaemonStatus();
    KLOG_INFO(QStringLiteral("DaemonController"),
              QStringLiteral("stopDaemonFromUi"),
              QStringLiteral("stop_daemon"),
              QStringLiteral("user_action"),
              QStringLiteral("systemd_or_fallback"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"success", result}});
    return result;
}

bool DaemonController::startTrayFromUi()
{
    const bool result = startTray();
    KLOG_INFO(QStringLiteral("DaemonController"),
              QStringLiteral("startTrayFromUi"),
              QStringLiteral("start_tray"),
              QStringLiteral("user_action"),
              QStringLiteral("process_start"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"success", result}});
    return result;
}

} // namespace khronicle
