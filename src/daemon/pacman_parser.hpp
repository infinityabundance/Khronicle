#pragma once

#include <optional>
#include <string>
#include <vector>

#include "models.hpp"

namespace khronicle {

struct PacmanParseResult {
    std::vector<KhronicleEvent> events;
    // Cursor is a string-encoded file offset; the daemon will store this in the meta table.
    std::string newCursor;
};

/**
 * Parse pacman.log from the given path, starting at the byte offset specified by previousCursor.
 *
 * - path: usually "/var/log/pacman.log"
 * - previousCursor: string representation of the last processed file offset,
 *   or std::nullopt if this is the first run.
 *
 * Returns:
 * - PacmanParseResult with all newly parsed KhronicleEvent entries and the updated offset as newCursor.
 */
PacmanParseResult parsePacmanLog(const std::string &path,
                                 const std::optional<std::string> &previousCursor);

} // namespace khronicle
