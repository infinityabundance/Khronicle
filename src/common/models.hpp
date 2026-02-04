#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/enums.hpp"

namespace khronicle {

struct HostIdentity {
    std::string hostId;
    std::string hostname;
    std::string displayName;
    std::string os;
    std::string hardware;
};

struct KhronicleEvent {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    EventCategory category;
    EventSource source;
    std::string summary;
    std::string details;
    nlohmann::json beforeState;
    nlohmann::json afterState;
    std::vector<std::string> relatedPackages;
    std::string hostId;
};

struct SystemSnapshot {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    std::string kernelVersion;
    nlohmann::json gpuDriver;
    nlohmann::json firmwareVersions;
    nlohmann::json keyPackages;
    HostIdentity hostIdentity;
};

struct KhronicleDiff {
    struct ChangedField {
        std::string path;
        nlohmann::json before;
        nlohmann::json after;
    };

    std::string snapshotAId;
    std::string snapshotBId;
    std::vector<ChangedField> changedFields;
};

enum class WatchScope {
    Event,
    Snapshot
};

enum class WatchSeverity {
    Info,
    Warning,
    Critical
};

struct WatchRule {
    std::string id;
    std::string name;
    std::string description;

    WatchScope scope;
    WatchSeverity severity;
    bool enabled = true;

    std::string categoryEquals;
    std::string riskLevelAtLeast;
    std::string packageNameContains;

    std::string activeFrom;
    std::string activeTo;

    nlohmann::json extra;
};

struct WatchSignal {
    std::string id;
    std::chrono::system_clock::time_point timestamp;

    std::string ruleId;
    std::string ruleName;
    WatchSeverity severity;

    std::string originType;
    std::string originId;

    std::string message;
};

} // namespace khronicle
