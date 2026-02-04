#include "daemon/pacman_parser.hpp"

#include <chrono>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "common/logging.hpp"

namespace khronicle {

namespace {

// pacman.log is append-only. We track a byte cursor so ingestion is incremental
// and does not re-parse the entire file on each daemon cycle.
// Cursor is a byte offset into pacman.log.
std::streampos parseCursor(const std::optional<std::string> &cursor)
{
    if (!cursor.has_value()) {
        return std::streampos(0);
    }

    try {
        return static_cast<std::streampos>(std::stoll(*cursor));
    } catch (const std::exception &) {
        return std::streampos(0);
    }
}

// Assumes pacman.log timestamps are local time, formatted as YYYY-MM-DD[T ]HH:MM.
std::optional<std::chrono::system_clock::time_point> parseTimestamp(
    const std::string &raw)
{
    std::string normalized = raw;
    std::replace(normalized.begin(), normalized.end(), 'T', ' ');

    std::tm tm{};
    std::istringstream stream(normalized);
    stream >> std::get_time(&tm, "%Y-%m-%d %H:%M");
    if (stream.fail()) {
        return std::nullopt;
    }

    std::time_t time = std::mktime(&tm);
    if (time == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }

    return std::chrono::system_clock::from_time_t(time);
}

std::string trim(const std::string &value)
{
    size_t start = 0;
    while (start < value.size()
           && std::isspace(static_cast<unsigned char>(value[start]))) {
        ++start;
    }
    size_t end = value.size();
    while (end > start
           && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(start, end - start);
}

struct ParsedLine {
    std::string timestamp;
    std::string operation;
    std::string packageName;
    std::string versionInfo;
};

std::optional<ParsedLine> parseLine(const std::string &line)
{
    // Pacman log lines are structured; we only care about install/upgrade/downgrade.
    static const std::regex pattern(
        R"(^\[(\d{4}-\d{2}-\d{2}[T ]\d{2}:\d{2})\]\s+\[ALPM\]\s+"
        R"((installed|upgraded|downgraded)\s+([^\s]+)\s+\(([^\)]*)\))");

    std::smatch match;
    if (!std::regex_search(line, match, pattern)) {
        return std::nullopt;
    }

    ParsedLine parsed;
    parsed.timestamp = match[1].str();
    parsed.operation = match[2].str();
    parsed.packageName = match[3].str();
    parsed.versionInfo = match[4].str();
    return parsed;
}

std::string buildSummary(const ParsedLine &parsed, const std::string &oldVersion,
                         const std::string &newVersion)
{
    if (parsed.operation == "installed") {
        return parsed.operation + " " + parsed.packageName + " " + newVersion;
    }

    if (!oldVersion.empty() && !newVersion.empty()) {
        return parsed.operation + " " + parsed.packageName + " " + oldVersion
            + " -> " + newVersion;
    }

    return parsed.operation + " " + parsed.packageName;
}

std::pair<std::string, std::string> splitVersions(const ParsedLine &parsed)
{
    if (parsed.operation == "installed") {
        return {"", trim(parsed.versionInfo)};
    }

    const std::string delimiter = "->";
    size_t pos = parsed.versionInfo.find(delimiter);
    if (pos == std::string::npos) {
        return {"", trim(parsed.versionInfo)};
    }

    std::string left = trim(parsed.versionInfo.substr(0, pos));
    std::string right = trim(parsed.versionInfo.substr(pos + delimiter.size()));
    return {left, right};
}

EventCategory categoryForPackage(const std::string &packageName)
{
    static const std::unordered_set<std::string> kernels{
        "linux",
        "linux-cachyos",
        "linux-zen",
        "linux-lts",
    };

    static const std::unordered_set<std::string> gpuDrivers{
        "mesa",
        "mesa-git",
        "nvidia",
        "nvidia-dkms",
        "nvidia-utils",
        "vulkan-radeon",
        "vulkan-intel",
        "vulkan-nouveau",
        "xf86-video-amdgpu",
        "xf86-video-intel",
    };

    static const std::unordered_set<std::string> firmware{
        "linux-firmware",
        "amd-ucode",
        "intel-ucode",
    };

    if (kernels.contains(packageName)) {
        return EventCategory::Kernel;
    }
    if (gpuDrivers.contains(packageName)) {
        return EventCategory::GpuDriver;
    }
    if (firmware.contains(packageName)) {
        return EventCategory::Firmware;
    }

    return EventCategory::Package;
}

bool isInterestingPackage(const std::string &packageName)
{
    // Explicit list keeps ingestion focused on kernel/GPU/firmware changes.
    // Explicit list of interesting packages for now (kernels, GPU drivers, firmware).
    static const std::unordered_set<std::string> interesting{
        "linux",
        "linux-cachyos",
        "linux-zen",
        "linux-lts",
        "mesa",
        "mesa-git",
        "nvidia",
        "nvidia-dkms",
        "nvidia-utils",
        "vulkan-radeon",
        "vulkan-intel",
        "vulkan-nouveau",
        "xf86-video-amdgpu",
        "xf86-video-intel",
        "linux-firmware",
        "amd-ucode",
        "intel-ucode",
    };

    return interesting.contains(packageName);
}

} // namespace

PacmanParseResult parsePacmanLog(const std::string &path,
                                 const std::optional<std::string> &previousCursor)
{
    // Parse pacman.log from a prior cursor and emit KhronicleEvent records.
    // If the log cannot be read, keep the previous cursor so we don't skip data.
    PacmanParseResult result;

    KLOG_DEBUG(QStringLiteral("PacmanParser"),
               QStringLiteral("parsePacmanLog"),
               QStringLiteral("parse_pacman_log"),
               QStringLiteral("ingestion_cycle"),
               QStringLiteral("incremental_cursor"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json{{"path", path},
                              {"cursor", previousCursor.value_or("")}});

    std::ifstream file(path);
    if (!file.is_open()) {
        // If the log can't be opened, keep the previous cursor so we don't skip data.
        result.newCursor = previousCursor.value_or("0");
        KLOG_WARN(QStringLiteral("PacmanParser"),
                  QStringLiteral("parsePacmanLog"),
                  QStringLiteral("pacman_log_open_failed"),
                  QStringLiteral("ingestion_cycle"),
                  QStringLiteral("file_open"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"path", path}});
        return result;
    }

    const std::streampos startPos = parseCursor(previousCursor);
    file.seekg(startPos);

    std::string line;
    while (std::getline(file, line)) {
        auto parsed = parseLine(line);
        if (!parsed.has_value()) {
            continue;
        }

        if (!isInterestingPackage(parsed->packageName)) {
            continue;
        }

        auto timestamp = parseTimestamp(parsed->timestamp);
        if (!timestamp.has_value()) {
            continue;
        }

        auto [oldVersion, newVersion] = splitVersions(*parsed);

        KhronicleEvent event;
        event.id = "pacman-" + parsed->timestamp + "-" + parsed->packageName
            + "-" + parsed->operation;
        event.timestamp = *timestamp;
        event.category = categoryForPackage(parsed->packageName);
        event.source = EventSource::Pacman;
        event.summary = buildSummary(*parsed, oldVersion, newVersion);
        event.details = line;
        event.beforeState = nlohmann::json::object();
        event.afterState = nlohmann::json::object();
        if (!oldVersion.empty()) {
            event.beforeState["version"] = oldVersion;
        }
        if (!newVersion.empty()) {
            event.afterState["version"] = newVersion;
        }
        event.relatedPackages = {parsed->packageName};

        result.events.push_back(std::move(event));
    }

    std::streampos endPos = file.tellg();
    if (endPos == std::streampos(-1)) {
        // tellg can return -1 after EOF; fall back to filesize.
        file.clear();
        file.seekg(0, std::ios::end);
        endPos = file.tellg();
    }

    if (endPos == std::streampos(-1)) {
        result.newCursor = previousCursor.value_or("0");
    } else {
        result.newCursor = std::to_string(static_cast<long long>(endPos));
    }

    KLOG_INFO(QStringLiteral("PacmanParser"),
              QStringLiteral("parsePacmanLog"),
              QStringLiteral("parse_pacman_log_complete"),
              QStringLiteral("ingestion_cycle"),
              QStringLiteral("incremental_cursor"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"events", result.events.size()},
                             {"newCursor", result.newCursor}});
    return result;
}

} // namespace khronicle
