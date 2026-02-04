#pragma once

#include <chrono>
#include <vector>

#include <QString>

#include "models.hpp"

namespace khronicle {

struct JournalParseResult {
    std::vector<KhronicleEvent> events;
    // The timestamp of the last processed journal entry, or the input 'since' if none were found.
    std::chrono::system_clock::time_point lastTimestamp;
    bool hadError = false;
};

/**
 * Parse systemd journal entries since a given time and extract firmware / GPU-driver related events.
 *
 * Implementation should internally call `journalctl` with an appropriate `--since` argument and
 * parse its output (short ISO format).
 */
JournalParseResult parseJournalSince(std::chrono::system_clock::time_point since);

} // namespace khronicle
