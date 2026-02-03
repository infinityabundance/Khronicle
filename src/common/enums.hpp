#pragma once

namespace khronicle {

enum class EventCategory {
    Kernel,
    GpuDriver,
    Firmware,
    Package,
    System
};

enum class EventSource {
    Pacman,
    Journal,
    Uname,
    Fwupd,
    Other
};

} // namespace khronicle
