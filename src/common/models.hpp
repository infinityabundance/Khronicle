#pragma once

#include <chrono>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "common/enums.hpp"

namespace khronicle {

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
    std::string riskLevel;
    std::string riskReason;
};

struct SystemSnapshot {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    std::string kernelVersion;
    nlohmann::json gpuDriver;
    nlohmann::json firmwareVersions;
    nlohmann::json keyPackages;
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

} // namespace khronicle
