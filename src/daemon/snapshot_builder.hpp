#pragma once

#include "models.hpp"

namespace khronicle {

/**
 * Build a snapshot of the current system state relevant to Khronicle:
 * - kernel version (uname -r)
 * - versions of key driver/system packages via pacman
 *
 * This function does not persist anything; it only interrogates the system
 * and returns a SystemSnapshot struct.
 */
SystemSnapshot buildCurrentSnapshot();

} // namespace khronicle
