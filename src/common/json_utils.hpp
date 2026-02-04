#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "common/models.hpp"

namespace khronicle {

inline std::string toIso8601Utc(std::chrono::system_clock::time_point timestamp)
{
    std::time_t time = std::chrono::system_clock::to_time_t(timestamp);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

inline std::chrono::system_clock::time_point fromIso8601Utc(const std::string &value)
{
    std::tm tm{};
    std::istringstream in(value);
    in >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    if (in.fail()) {
        return std::chrono::system_clock::time_point{};
    }
#if defined(_WIN32)
    std::time_t time = _mkgmtime(&tm);
#else
    std::time_t time = timegm(&tm);
#endif
    if (time == static_cast<std::time_t>(-1)) {
        return std::chrono::system_clock::time_point{};
    }
    return std::chrono::system_clock::from_time_t(time);
}

inline std::string toCategoryString(EventCategory category)
{
    switch (category) {
    case EventCategory::Kernel:
        return "kernel";
    case EventCategory::GpuDriver:
        return "gpu_driver";
    case EventCategory::Firmware:
        return "firmware";
    case EventCategory::Package:
        return "package";
    case EventCategory::System:
        return "system";
    }
    return "system";
}

inline std::string toSourceString(EventSource source)
{
    switch (source) {
    case EventSource::Pacman:
        return "pacman";
    case EventSource::Journal:
        return "journal";
    case EventSource::Uname:
        return "uname";
    case EventSource::Fwupd:
        return "fwupd";
    case EventSource::Other:
        return "other";
    }
    return "other";
}

inline EventCategory parseCategoryString(const std::string &value)
{
    if (value == "kernel") {
        return EventCategory::Kernel;
    }
    if (value == "gpu_driver") {
        return EventCategory::GpuDriver;
    }
    if (value == "firmware") {
        return EventCategory::Firmware;
    }
    if (value == "package") {
        return EventCategory::Package;
    }
    if (value == "system") {
        return EventCategory::System;
    }
    return EventCategory::System;
}

inline EventSource parseSourceString(const std::string &value)
{
    if (value == "pacman") {
        return EventSource::Pacman;
    }
    if (value == "journal") {
        return EventSource::Journal;
    }
    if (value == "uname") {
        return EventSource::Uname;
    }
    if (value == "fwupd") {
        return EventSource::Fwupd;
    }
    if (value == "other") {
        return EventSource::Other;
    }
    return EventSource::Other;
}

inline void to_json(nlohmann::json &j, const EventCategory &category)
{
    j = toCategoryString(category);
}

inline void from_json(const nlohmann::json &j, EventCategory &category)
{
    if (j.is_string()) {
        category = parseCategoryString(j.get<std::string>());
    } else {
        category = EventCategory::System;
    }
}

inline void to_json(nlohmann::json &j, const EventSource &source)
{
    j = toSourceString(source);
}

inline void from_json(const nlohmann::json &j, EventSource &source)
{
    if (j.is_string()) {
        source = parseSourceString(j.get<std::string>());
    } else {
        source = EventSource::Other;
    }
}

inline void to_json(nlohmann::json &j, const KhronicleEvent &event)
{
    j = nlohmann::json{
        {"id", event.id},
        {"timestamp", toIso8601Utc(event.timestamp)},
        {"category", event.category},
        {"source", event.source},
        {"summary", event.summary},
        {"details", event.details},
        {"beforeState", event.beforeState},
        {"afterState", event.afterState},
        {"relatedPackages", event.relatedPackages},
        {"riskLevel", event.riskLevel.empty() ? "info" : event.riskLevel},
        {"riskReason", event.riskReason},
        {"provenance", nlohmann::json{
            {"sourceType", event.provenance.sourceType},
            {"sourceRef", event.provenance.sourceRef},
            {"parserVersion", event.provenance.parserVersion},
            {"ingestionId", event.provenance.ingestionId}
        }}
    };
}

inline void from_json(const nlohmann::json &j, KhronicleEvent &event)
{
    event.id = j.value("id", "");
    event.timestamp = fromIso8601Utc(j.value("timestamp", ""));
    if (j.contains("category")) {
        event.category = j.at("category").get<EventCategory>();
    } else {
        event.category = EventCategory::System;
    }
    if (j.contains("source")) {
        event.source = j.at("source").get<EventSource>();
    } else {
        event.source = EventSource::Other;
    }
    event.summary = j.value("summary", "");
    event.details = j.value("details", "");
    if (j.contains("beforeState")) {
        event.beforeState = j.at("beforeState");
    } else {
        event.beforeState = nlohmann::json::object();
    }
    if (j.contains("afterState")) {
        event.afterState = j.at("afterState");
    } else {
        event.afterState = nlohmann::json::object();
    }
    if (j.contains("relatedPackages") && j.at("relatedPackages").is_array()) {
        event.relatedPackages = j.at("relatedPackages").get<std::vector<std::string>>();
    } else {
        event.relatedPackages.clear();
    }
    event.riskLevel = j.value("riskLevel", "info");
    event.riskReason = j.value("riskReason", "");
    if (j.contains("provenance") && j.at("provenance").is_object()) {
        const auto &prov = j.at("provenance");
        event.provenance.sourceType = prov.value("sourceType", "unknown");
        event.provenance.sourceRef = prov.value("sourceRef", "");
        event.provenance.parserVersion = prov.value("parserVersion", "legacy");
        event.provenance.ingestionId = prov.value("ingestionId", "");
    } else {
        event.provenance.sourceType = "unknown";
        event.provenance.sourceRef.clear();
        event.provenance.parserVersion = "legacy";
        event.provenance.ingestionId.clear();
    }
}

inline void to_json(nlohmann::json &j, const SystemSnapshot &snapshot)
{
    j = nlohmann::json{
        {"id", snapshot.id},
        {"timestamp", toIso8601Utc(snapshot.timestamp)},
        {"kernelVersion", snapshot.kernelVersion},
        {"gpuDriver", snapshot.gpuDriver},
        {"firmwareVersions", snapshot.firmwareVersions},
        {"keyPackages", snapshot.keyPackages},
        {"snapshotId", snapshot.snapshotId},
        {"ingestionId", snapshot.ingestionId},
        {"kernelSource", snapshot.kernelSource}
    };
}

inline void from_json(const nlohmann::json &j, SystemSnapshot &snapshot)
{
    snapshot.id = j.value("id", "");
    snapshot.timestamp = fromIso8601Utc(j.value("timestamp", ""));
    snapshot.kernelVersion = j.value("kernelVersion", "");
    if (j.contains("gpuDriver")) {
        snapshot.gpuDriver = j.at("gpuDriver");
    } else {
        snapshot.gpuDriver = nlohmann::json::object();
    }
    if (j.contains("firmwareVersions")) {
        snapshot.firmwareVersions = j.at("firmwareVersions");
    } else {
        snapshot.firmwareVersions = nlohmann::json::object();
    }
    if (j.contains("keyPackages")) {
        snapshot.keyPackages = j.at("keyPackages");
    } else {
        snapshot.keyPackages = nlohmann::json::object();
    }
    snapshot.snapshotId = j.value("snapshotId", snapshot.id);
    snapshot.ingestionId = j.value("ingestionId", "");
    snapshot.kernelSource = j.value("kernelSource", "");
}

inline void to_json(nlohmann::json &j, const AuditLogEntry &entry)
{
    j = nlohmann::json{
        {"id", entry.id},
        {"timestamp", toIso8601Utc(entry.timestamp)},
        {"auditType", entry.auditType},
        {"inputRefs", entry.inputRefs},
        {"method", entry.method},
        {"outputSummary", entry.outputSummary}
    };
}

inline void from_json(const nlohmann::json &j, AuditLogEntry &entry)
{
    entry.id = j.value("id", "");
    entry.timestamp = fromIso8601Utc(j.value("timestamp", ""));
    entry.auditType = j.value("auditType", "");
    if (j.contains("inputRefs") && j.at("inputRefs").is_array()) {
        entry.inputRefs = j.at("inputRefs").get<std::vector<std::string>>();
    } else {
        entry.inputRefs.clear();
    }
    entry.method = j.value("method", "");
    entry.outputSummary = j.value("outputSummary", "");
}

inline void to_json(nlohmann::json &j, const KhronicleDiff::ChangedField &field)
{
    j = nlohmann::json{{"path", field.path}, {"before", field.before}, {"after", field.after}};
}

inline void from_json(const nlohmann::json &j, KhronicleDiff::ChangedField &field)
{
    field.path = j.value("path", "");
    if (j.contains("before")) {
        field.before = j.at("before");
    } else {
        field.before = nlohmann::json::object();
    }
    if (j.contains("after")) {
        field.after = j.at("after");
    } else {
        field.after = nlohmann::json::object();
    }
}

inline void to_json(nlohmann::json &j, const KhronicleDiff &diff)
{
    j = nlohmann::json{
        {"snapshotAId", diff.snapshotAId},
        {"snapshotBId", diff.snapshotBId},
        {"changedFields", diff.changedFields}
    };
}

inline void from_json(const nlohmann::json &j, KhronicleDiff &diff)
{
    diff.snapshotAId = j.value("snapshotAId", "");
    diff.snapshotBId = j.value("snapshotBId", "");
    if (j.contains("changedFields") && j.at("changedFields").is_array()) {
        diff.changedFields = j.at("changedFields").get<std::vector<KhronicleDiff::ChangedField>>();
    } else {
        diff.changedFields.clear();
    }
}

} // namespace khronicle
