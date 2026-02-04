#include "daemon/pacman_parser.hpp"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

constexpr std::size_t kMaxBytesPerRun = 5 * 1024 * 1024; // 5 MiB

// Cursor is a byte offset into pacman.log.
std::optional<std::streampos> parseCursor(const std::optional<std::string> &cursor)
{
    if (!cursor.has_value()) {
        return std::nullopt;
    }

    try {
        const long long value = std::stoll(*cursor);
        if (value < 0) {
            return std::nullopt;
        }
        return static_cast<std::streampos>(value);
    } catch (const std::exception &) {
        return std::nullopt;
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
    // Expected format: [YYYY-MM-DD HH:MM] [ALPM] <operation> <package> (<versions>)
    const std::string alpmTag = " [ALPM] ";
    const std::string ops[] = {"installed", "upgraded", "downgraded"};

    if (line.empty() || line[0] != '[') {
        return std::nullopt;
    }

    const size_t tsEnd = line.find(']');
    if (tsEnd == std::string::npos) {
        return std::nullopt;
    }

    const size_t alpmPos = line.find(alpmTag, tsEnd);
    if (alpmPos == std::string::npos) {
        return std::nullopt;
    }

    const size_t opStart = alpmPos + alpmTag.size();
    const size_t opEnd = line.find(' ', opStart);
    if (opEnd == std::string::npos) {
        return std::nullopt;
    }

    const std::string operation = line.substr(opStart, opEnd - opStart);
    bool opOk = false;
    for (const auto &op : ops) {
        if (operation == op) {
            opOk = true;
            break;
        }
    }
    if (!opOk) {
        return std::nullopt;
    }

    const size_t pkgStart = opEnd + 1;
    const size_t pkgEnd = line.find(' ', pkgStart);
    if (pkgEnd == std::string::npos) {
        return std::nullopt;
    }

    const size_t openParen = line.find('(', pkgEnd);
    const size_t closeParen = line.find(')', openParen);
    if (openParen == std::string::npos || closeParen == std::string::npos
        || closeParen <= openParen + 1) {
        return std::nullopt;
    }

    ParsedLine parsed;
    parsed.timestamp = line.substr(1, tsEnd - 1);
    parsed.operation = operation;
    parsed.packageName = line.substr(pkgStart, pkgEnd - pkgStart);
    parsed.versionInfo = line.substr(openParen + 1, closeParen - openParen - 1);
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
    PacmanParseResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        // If the log can't be opened, keep the previous cursor so we don't skip data.
        result.newCursor = previousCursor.value_or("0");
        result.hadError = true;
        return result;
    }

    std::error_code error;
    const std::uintmax_t fileSize = std::filesystem::file_size(path, error);

    std::streampos startPos = std::streampos(0);
    if (const auto cursor = parseCursor(previousCursor)) {
        startPos = *cursor;
    }

    if (!error && fileSize > 0) {
        const std::streamoff startOff = startPos;
        if (startOff > 0 && static_cast<std::uintmax_t>(startOff) > fileSize) {
            // Log rotation or truncation detected: reset cursor to start of file.
            std::cerr << "Khronicle: pacman.log cursor beyond file size; resetting.\n";
            startPos = std::streampos(0);
        }
    }

    file.seekg(startPos);

    std::string line;
    // Cap bytes processed per run to keep ingestion cycles bounded.
    std::size_t bytesConsumed = 0;
    std::streampos lastPos = file.tellg();
    while (std::getline(file, line)) {
        bytesConsumed += line.size() + 1;
        lastPos = file.tellg();

        auto parsed = parseLine(line);
        if (!parsed.has_value()) {
            if (bytesConsumed >= kMaxBytesPerRun) {
                break;
            }
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
        event.riskLevel = "info";
        event.riskReason.clear();
        if (!oldVersion.empty()) {
            event.beforeState["version"] = oldVersion;
        }
        if (!newVersion.empty()) {
            event.afterState["version"] = newVersion;
        }
        event.relatedPackages = {parsed->packageName};

        result.events.push_back(std::move(event));

        if (bytesConsumed >= kMaxBytesPerRun) {
            break;
        }
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
        const std::streampos cursorPos =
            (bytesConsumed >= kMaxBytesPerRun && lastPos != std::streampos(-1))
            ? lastPos
            : endPos;
        result.newCursor = std::to_string(static_cast<long long>(cursorPos));
    }

    return result;
}

} // namespace khronicle
