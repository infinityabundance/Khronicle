#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "common/models.hpp"

namespace khronicle {

// KhronicleStore is the SQLite access layer for all persistent data:
// events, snapshots, meta, host identity, watch rules, and watch signals.
class KhronicleStore {
public:
    KhronicleStore();
    ~KhronicleStore();

    // Event and snapshot persistence API used by the daemon.
    void addEvent(const KhronicleEvent &event);
    void addSnapshot(const SystemSnapshot &snapshot);
    HostIdentity getHostIdentity() const;

    // Watchpoint rules and signals. Rules are editable, signals are append-only.
    std::vector<WatchRule> listWatchRules() const;
    void upsertWatchRule(const WatchRule &rule);
    void deleteWatchRule(const std::string &id);

    void addWatchSignal(const WatchSignal &signal);
    std::vector<WatchSignal> getWatchSignalsSince(
        std::chrono::system_clock::time_point t) const;

    // Query event history for API and reports.
    std::vector<KhronicleEvent> getEventsSince(
        std::chrono::system_clock::time_point since) const;
    std::vector<KhronicleEvent> getEventsBetween(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to) const;

    std::vector<SystemSnapshot> listSnapshots() const;
    std::optional<SystemSnapshot> getSnapshot(const std::string &id) const;
    std::optional<SystemSnapshot> getSnapshotBefore(
        std::chrono::system_clock::time_point t) const;
    std::optional<SystemSnapshot> getSnapshotAfter(
        std::chrono::system_clock::time_point t) const;

    KhronicleDiff diffSnapshots(const std::string &aId, const std::string &bId) const;

    std::optional<std::string> getMeta(const std::string &key) const;
    void setMeta(const std::string &key, const std::string &value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace khronicle
