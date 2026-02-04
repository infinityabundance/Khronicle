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
