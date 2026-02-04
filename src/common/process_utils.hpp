#pragma once

#include <QString>

namespace khronicle {

bool isDaemonRunning();
bool startDaemon();
bool stopDaemon();

bool isTrayRunning();
bool startTray();

QString daemonSocketPath();

} // namespace khronicle
