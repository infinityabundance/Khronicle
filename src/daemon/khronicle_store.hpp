#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/models.hpp"

namespace khronicle {

class KhronicleStore {
public:
    KhronicleStore();
    ~KhronicleStore();

    void addEvent(const KhronicleEvent &event);
    void addSnapshot(const SystemSnapshot &snapshot);

    std::vector<KhronicleEvent> getEventsSince(
        std::chrono::system_clock::time_point since) const;
    std::vector<KhronicleEvent> getEventsBetween(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) const;

    std::vector<SystemSnapshot> listSnapshots() const;
    std::optional<SystemSnapshot> getSnapshot(const std::string &id) const;

    KhronicleDiff diffSnapshots(const std::string &aId, const std::string &bId);

    std::optional<std::string> getMeta(const std::string &key) const;
    void setMeta(const std::string &key, const std::string &value);

    // Returns true if SQLite integrity_check reports ok.
    bool integrityCheck(std::string *message = nullptr) const;

    std::optional<nlohmann::json> getEventProvenance(const std::string &id) const;
    void addAuditLog(const AuditLogEntry &entry);
    void addRiskAuditIfNeeded(const KhronicleEvent &event);
    std::vector<AuditLogEntry> getAuditLogSince(
        std::chrono::system_clock::time_point since,
        const std::optional<std::string> &type) const;

    std::string schemaSql() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace khronicle
