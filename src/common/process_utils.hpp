#pragma once

#include <QString>

namespace khronicle {

bool isDaemonRunning();
bool startDaemon();
bool stopDaemon();

bool isTrayRunning();
bool startTray();
bool startUi();

QString daemonSocketPath();

} // namespace khronicle
