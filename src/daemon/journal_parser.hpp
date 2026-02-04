#pragma once

#include <chrono>
#include <vector>

#include <QString>
#include <QStringList>

#include "models.hpp"

namespace khronicle {

struct JournalParseResult {
    std::vector<KhronicleEvent> events;
    // The timestamp of the last processed journal entry, or the input 'since' if none were found.
    std::chrono::system_clock::time_point lastTimestamp;
};

/**
 * Parse systemd journal entries since a given time and extract firmware / GPU-driver related events.
 *
 * Implementation should internally call `journalctl` with an appropriate `--since` argument and
 * parse its output (short ISO format).
 */
JournalParseResult parseJournalSince(std::chrono::system_clock::time_point since);

// Parse pre-fetched journalctl output lines (short-iso format). This is used for tests
// and for isolating parsing logic from the system journal invocation.
JournalParseResult parseJournalOutputLines(const QStringList &lines,
                                           std::chrono::system_clock::time_point since);

} // namespace khronicle
