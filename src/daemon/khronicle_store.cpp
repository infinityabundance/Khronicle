#include "daemon/khronicle_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <sstream>
#include <set>
#include <unistd.h>
#include <stdexcept>

#include <sqlite3.h>

namespace khronicle {

namespace {

constexpr const char *kCreateEventsTable =
    "CREATE TABLE IF NOT EXISTS events ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    category INTEGER NOT NULL,"
    "    source INTEGER NOT NULL,"
    "    summary TEXT NOT NULL,"
    "    details TEXT,"
    "    before_state TEXT,"
    "    after_state TEXT,"
    "    related_packages TEXT,"
    "    host_id TEXT"
    ");";

constexpr const char *kCreateSnapshotsTable =
    "CREATE TABLE IF NOT EXISTS snapshots ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    kernel_version TEXT NOT NULL,"
    "    gpu_driver TEXT,"
    "    firmware_versions TEXT,"
    "    key_packages TEXT,"
    "    host_id TEXT"
    ");";

constexpr const char *kCreateMetaTable =
    "CREATE TABLE IF NOT EXISTS meta ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT NOT NULL"
    ");";

constexpr const char *kCreateHostIdentityTable =
    "CREATE TABLE IF NOT EXISTS host_identity ("
    "    host_id TEXT PRIMARY KEY,"
    "    hostname TEXT NOT NULL,"
    "    display_name TEXT,"
    "    os TEXT,"
    "    hardware TEXT"
    ");";

class Statement {
public:
    Statement(sqlite3 *db, const char *sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            throw std::runtime_error("sqlite prepare failed");
        }
    }

    ~Statement()
    {
        if (stmt) {
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_stmt *get() const
    {
        return stmt;
    }

private:
    sqlite3_stmt *stmt = nullptr;
};

int64_t toEpochSeconds(std::chrono::system_clock::time_point timestamp)
{
    return std::chrono::duration_cast<std::chrono::seconds>(
               timestamp.time_since_epoch())
        .count();
}

std::chrono::system_clock::time_point fromEpochSeconds(int64_t value)
{
    return std::chrono::system_clock::time_point{
        std::chrono::seconds{value}};
}

void execOrThrow(sqlite3 *db, const char *sql)
{
    char *error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "sqlite exec failed";
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

bool columnExists(sqlite3 *db, const std::string &table, const std::string &column)
{
    const std::string sql = "PRAGMA table_info(" + table + ");";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (name && column == name) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

std::string generateUuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    const uint64_t part1 = dist(gen);
    const uint64_t part2 = dist(gen);

    std::ostringstream out;
    out << std::hex;
    out << (part1 >> 32);
    out << "-";
    out << ((part1 >> 16) & 0xFFFF);
    out << "-";
    out << (part1 & 0xFFFF);
    out << "-";
    out << (part2 >> 48);
    out << "-";
    out << (part2 & 0xFFFFFFFFFFFFULL);
    return out.str();
}

void bindText(sqlite3_stmt *stmt, int index, const std::string &value)
{
    sqlite3_bind_text(stmt, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void bindOptionalText(sqlite3_stmt *stmt, int index, const std::string &value)
{
    if (value.empty()) {
        sqlite3_bind_null(stmt, index);
        return;
    }
    bindText(stmt, index, value);
}

void bindJson(sqlite3_stmt *stmt, int index, const nlohmann::json &value)
{
    if (value.is_null()) {
        sqlite3_bind_null(stmt, index);
        return;
    }
    bindText(stmt, index, value.dump());
}

std::string columnText(sqlite3_stmt *stmt, int index)
{
    const unsigned char *text = sqlite3_column_text(stmt, index);
    if (!text) {
        return {};
    }
    return reinterpret_cast<const char *>(text);
}

nlohmann::json columnJson(sqlite3_stmt *stmt, int index)
{
    const unsigned char *text = sqlite3_column_text(stmt, index);
    if (!text) {
        return nlohmann::json::object();
    }
    try {
        return nlohmann::json::parse(reinterpret_cast<const char *>(text));
    } catch (const nlohmann::json::parse_error &) {
        return nlohmann::json::object();
    }
}

EventCategory categoryFromInt(int value)
{
    switch (value) {
    case 0:
        return EventCategory::Kernel;
    case 1:
        return EventCategory::GpuDriver;
    case 2:
        return EventCategory::Firmware;
    case 3:
        return EventCategory::Package;
    case 4:
        return EventCategory::System;
    default:
        return EventCategory::System;
    }
}

EventSource sourceFromInt(int value)
{
    switch (value) {
    case 0:
        return EventSource::Pacman;
    case 1:
        return EventSource::Journal;
    case 2:
        return EventSource::Uname;
    case 3:
        return EventSource::Fwupd;
    case 4:
        return EventSource::Other;
    default:
        return EventSource::Other;
    }
}

} // namespace

struct KhronicleStore::Impl {
    sqlite3 *db = nullptr;
    HostIdentity hostIdentity;
};

KhronicleStore::KhronicleStore()
    : impl(std::make_unique<Impl>())
{
    const char *home = std::getenv("HOME");
    std::filesystem::path basePath = home ? home : ".";
    basePath /= ".local/share/khronicle";
    std::filesystem::create_directories(basePath);

    std::filesystem::path dbPath = basePath / "khronicle.db";
    if (sqlite3_open(dbPath.string().c_str(), &impl->db) != SQLITE_OK) {
        throw std::runtime_error("failed to open khronicle database");
    }

    execOrThrow(impl->db, kCreateEventsTable);
    execOrThrow(impl->db, kCreateSnapshotsTable);
    execOrThrow(impl->db, kCreateMetaTable);
    execOrThrow(impl->db, kCreateHostIdentityTable);

    // Load or initialize host identity (stable per database).
    {
        Statement stmt(impl->db,
                       "SELECT host_id, hostname, display_name, os, hardware "
                       "FROM host_identity LIMIT 1;");
        if (sqlite3_step(stmt.get()) == SQLITE_ROW) {
            impl->hostIdentity.hostId = columnText(stmt.get(), 0);
            impl->hostIdentity.hostname = columnText(stmt.get(), 1);
            impl->hostIdentity.displayName = columnText(stmt.get(), 2);
            impl->hostIdentity.os = columnText(stmt.get(), 3);
            impl->hostIdentity.hardware = columnText(stmt.get(), 4);
        } else {
            char hostnameBuf[256] = {};
            if (gethostname(hostnameBuf, sizeof(hostnameBuf)) != 0) {
                hostnameBuf[0] = '\0';
            }

            impl->hostIdentity.hostId = generateUuid();
            impl->hostIdentity.hostname = hostnameBuf;
            impl->hostIdentity.displayName.clear();
            impl->hostIdentity.os = "Linux";
            impl->hostIdentity.hardware.clear();

            Statement insertStmt(impl->db,
                                 "INSERT INTO host_identity (host_id, hostname, "
                                 "display_name, os, hardware) VALUES (?, ?, ?, ?, ?);");
            bindText(insertStmt.get(), 1, impl->hostIdentity.hostId);
            bindText(insertStmt.get(), 2, impl->hostIdentity.hostname);
            bindOptionalText(insertStmt.get(), 3, impl->hostIdentity.displayName);
            bindOptionalText(insertStmt.get(), 4, impl->hostIdentity.os);
            bindOptionalText(insertStmt.get(), 5, impl->hostIdentity.hardware);

            if (sqlite3_step(insertStmt.get()) != SQLITE_DONE) {
                throw std::runtime_error("failed to insert host identity");
            }
        }
    }

    if (!columnExists(impl->db, "events", "host_id")) {
        execOrThrow(impl->db, "ALTER TABLE events ADD COLUMN host_id TEXT;");
    }
    if (!columnExists(impl->db, "snapshots", "host_id")) {
        execOrThrow(impl->db, "ALTER TABLE snapshots ADD COLUMN host_id TEXT;");
    }
}

KhronicleStore::~KhronicleStore()
{
    if (impl && impl->db) {
        sqlite3_close(impl->db);
        impl->db = nullptr;
    }
}

HostIdentity KhronicleStore::getHostIdentity() const
{
    return impl->hostIdentity;
}

void KhronicleStore::addEvent(const KhronicleEvent &event)
{
    Statement stmt(impl->db,
                   "INSERT OR REPLACE INTO events (id, timestamp, category, "
                   "source, summary, details, before_state, after_state, "
                   "related_packages, host_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    bindText(stmt.get(), 1, event.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(event.timestamp));
    sqlite3_bind_int(stmt.get(), 3, static_cast<int>(event.category));
    sqlite3_bind_int(stmt.get(), 4, static_cast<int>(event.source));
    bindText(stmt.get(), 5, event.summary);
    bindOptionalText(stmt.get(), 6, event.details);
    bindJson(stmt.get(), 7, event.beforeState);
    bindJson(stmt.get(), 8, event.afterState);
    bindJson(stmt.get(), 9, nlohmann::json(event.relatedPackages));
    bindText(stmt.get(), 10, event.hostId.empty() ? impl->hostIdentity.hostId : event.hostId);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert event");
    }
}

void KhronicleStore::addSnapshot(const SystemSnapshot &snapshot)
{
    Statement stmt(impl->db,
                   "INSERT OR REPLACE INTO snapshots (id, timestamp, "
                   "kernel_version, gpu_driver, firmware_versions, "
                   "key_packages, host_id) VALUES (?, ?, ?, ?, ?, ?, ?);");
    bindText(stmt.get(), 1, snapshot.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(snapshot.timestamp));
    bindText(stmt.get(), 3, snapshot.kernelVersion);
    bindJson(stmt.get(), 4, snapshot.gpuDriver);
    bindJson(stmt.get(), 5, snapshot.firmwareVersions);
    bindJson(stmt.get(), 6, snapshot.keyPackages);
    bindText(stmt.get(), 7, snapshot.hostIdentity.hostId.empty()
        ? impl->hostIdentity.hostId
        : snapshot.hostIdentity.hostId);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert snapshot");
    }
}

std::vector<KhronicleEvent> KhronicleStore::getEventsSince(
    std::chrono::system_clock::time_point since) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, category, source, summary, details, "
                   "before_state, after_state, related_packages, host_id "
                   "FROM events WHERE timestamp >= ? ORDER BY timestamp ASC;");
    sqlite3_bind_int64(stmt.get(), 1, toEpochSeconds(since));

    std::vector<KhronicleEvent> events;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        KhronicleEvent event;
        event.id = columnText(stmt.get(), 0);
        event.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
        event.category = categoryFromInt(sqlite3_column_int(stmt.get(), 2));
        event.source = sourceFromInt(sqlite3_column_int(stmt.get(), 3));
        event.summary = columnText(stmt.get(), 4);
        event.details = columnText(stmt.get(), 5);
        event.beforeState = columnJson(stmt.get(), 6);
        event.afterState = columnJson(stmt.get(), 7);
        nlohmann::json related = columnJson(stmt.get(), 8);
        if (related.is_array()) {
            event.relatedPackages = related.get<std::vector<std::string>>();
        }
        event.hostId = columnText(stmt.get(), 9);
        if (event.hostId.empty()) {
            event.hostId = impl->hostIdentity.hostId;
        }
        events.push_back(std::move(event));
    }

    return events;
}

std::vector<KhronicleEvent> KhronicleStore::getEventsBetween(
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, category, source, summary, details, "
                   "before_state, after_state, related_packages, host_id "
                   "FROM events WHERE timestamp >= ? AND timestamp <= ? "
                   "ORDER BY timestamp ASC;");
    sqlite3_bind_int64(stmt.get(), 1, toEpochSeconds(from));
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(to));

    std::vector<KhronicleEvent> events;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        KhronicleEvent event;
        event.id = columnText(stmt.get(), 0);
        event.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
        event.category = categoryFromInt(sqlite3_column_int(stmt.get(), 2));
        event.source = sourceFromInt(sqlite3_column_int(stmt.get(), 3));
        event.summary = columnText(stmt.get(), 4);
        event.details = columnText(stmt.get(), 5);
        event.beforeState = columnJson(stmt.get(), 6);
        event.afterState = columnJson(stmt.get(), 7);
        nlohmann::json related = columnJson(stmt.get(), 8);
        if (related.is_array()) {
            event.relatedPackages = related.get<std::vector<std::string>>();
        }
        event.hostId = columnText(stmt.get(), 9);
        if (event.hostId.empty()) {
            event.hostId = impl->hostIdentity.hostId;
        }
        events.push_back(std::move(event));
    }

    return events;
}

std::vector<SystemSnapshot> KhronicleStore::listSnapshots() const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages, host_id "
                   "FROM snapshots ORDER BY timestamp ASC;");

    std::vector<SystemSnapshot> snapshots;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        SystemSnapshot snapshot;
        snapshot.id = columnText(stmt.get(), 0);
        snapshot.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
        snapshot.kernelVersion = columnText(stmt.get(), 2);
        snapshot.gpuDriver = columnJson(stmt.get(), 3);
        snapshot.firmwareVersions = columnJson(stmt.get(), 4);
        snapshot.keyPackages = columnJson(stmt.get(), 5);
        const std::string hostId = columnText(stmt.get(), 6);
        snapshot.hostIdentity = impl->hostIdentity;
        if (!hostId.empty()) {
            snapshot.hostIdentity.hostId = hostId;
        }
        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

std::optional<SystemSnapshot> KhronicleStore::getSnapshot(
    const std::string &id) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages, host_id "
                   "FROM snapshots WHERE id = ? LIMIT 1;");
    bindText(stmt.get(), 1, id);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    SystemSnapshot snapshot;
    snapshot.id = columnText(stmt.get(), 0);
    snapshot.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
    snapshot.kernelVersion = columnText(stmt.get(), 2);
    snapshot.gpuDriver = columnJson(stmt.get(), 3);
    snapshot.firmwareVersions = columnJson(stmt.get(), 4);
    snapshot.keyPackages = columnJson(stmt.get(), 5);
    const std::string hostId = columnText(stmt.get(), 6);
    snapshot.hostIdentity = impl->hostIdentity;
    if (!hostId.empty()) {
        snapshot.hostIdentity.hostId = hostId;
    }

    return snapshot;
}

std::optional<SystemSnapshot> KhronicleStore::getSnapshotBefore(
    std::chrono::system_clock::time_point t) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages, host_id "
                   "FROM snapshots WHERE timestamp <= ? "
                   "ORDER BY timestamp DESC LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, toEpochSeconds(t));

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    SystemSnapshot snapshot;
    snapshot.id = columnText(stmt.get(), 0);
    snapshot.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
    snapshot.kernelVersion = columnText(stmt.get(), 2);
    snapshot.gpuDriver = columnJson(stmt.get(), 3);
    snapshot.firmwareVersions = columnJson(stmt.get(), 4);
    snapshot.keyPackages = columnJson(stmt.get(), 5);
    const std::string hostId = columnText(stmt.get(), 6);
    snapshot.hostIdentity = impl->hostIdentity;
    if (!hostId.empty()) {
        snapshot.hostIdentity.hostId = hostId;
    }
    return snapshot;
}

std::optional<SystemSnapshot> KhronicleStore::getSnapshotAfter(
    std::chrono::system_clock::time_point t) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages, host_id "
                   "FROM snapshots WHERE timestamp >= ? "
                   "ORDER BY timestamp ASC LIMIT 1;");
    sqlite3_bind_int64(stmt.get(), 1, toEpochSeconds(t));

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    SystemSnapshot snapshot;
    snapshot.id = columnText(stmt.get(), 0);
    snapshot.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
    snapshot.kernelVersion = columnText(stmt.get(), 2);
    snapshot.gpuDriver = columnJson(stmt.get(), 3);
    snapshot.firmwareVersions = columnJson(stmt.get(), 4);
    snapshot.keyPackages = columnJson(stmt.get(), 5);
    const std::string hostId = columnText(stmt.get(), 6);
    snapshot.hostIdentity = impl->hostIdentity;
    if (!hostId.empty()) {
        snapshot.hostIdentity.hostId = hostId;
    }
    return snapshot;
}

KhronicleDiff KhronicleStore::diffSnapshots(const std::string &aId,
                                            const std::string &bId) const
{
    KhronicleDiff diff;
    diff.snapshotAId = aId;
    diff.snapshotBId = bId;

    auto snapshotA = getSnapshot(aId);
    auto snapshotB = getSnapshot(bId);
    if (!snapshotA || !snapshotB) {
        return diff;
    }

    if (snapshotA->kernelVersion != snapshotB->kernelVersion) {
        diff.changedFields.push_back({
            "kernelVersion",
            snapshotA->kernelVersion,
            snapshotB->kernelVersion,
        });
    }

    if (snapshotA->gpuDriver != snapshotB->gpuDriver) {
        diff.changedFields.push_back({
            "gpuDriver",
            snapshotA->gpuDriver,
            snapshotB->gpuDriver,
        });
    }

    if (snapshotA->firmwareVersions != snapshotB->firmwareVersions) {
        diff.changedFields.push_back({
            "firmwareVersions",
            snapshotA->firmwareVersions,
            snapshotB->firmwareVersions,
        });
    }

    nlohmann::json keyPackagesA = snapshotA->keyPackages.is_object()
        ? snapshotA->keyPackages
        : nlohmann::json::object();
    nlohmann::json keyPackagesB = snapshotB->keyPackages.is_object()
        ? snapshotB->keyPackages
        : nlohmann::json::object();

    std::set<std::string> keys;
    for (const auto &item : keyPackagesA.items()) {
        keys.insert(item.key());
    }
    for (const auto &item : keyPackagesB.items()) {
        keys.insert(item.key());
    }

    for (const auto &key : keys) {
        nlohmann::json before = keyPackagesA.contains(key)
            ? keyPackagesA.at(key)
            : nlohmann::json();
        nlohmann::json after = keyPackagesB.contains(key)
            ? keyPackagesB.at(key)
            : nlohmann::json();
        if (before != after) {
            diff.changedFields.push_back({
                "keyPackages." + key,
                before,
                after,
            });
        }
    }

    return diff;
}

std::optional<std::string> KhronicleStore::getMeta(const std::string &key) const
{
    Statement stmt(impl->db,
                   "SELECT value FROM meta WHERE key = ? LIMIT 1;");
    bindText(stmt.get(), 1, key);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    return columnText(stmt.get(), 0);
}

void KhronicleStore::setMeta(const std::string &key, const std::string &value)
{
    Statement stmt(impl->db,
                   "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?);");
    bindText(stmt.get(), 1, key);
    bindText(stmt.get(), 2, value);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to set meta value");
    }
}

} // namespace khronicle
