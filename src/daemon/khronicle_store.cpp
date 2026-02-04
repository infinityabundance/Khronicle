#include "daemon/khronicle_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
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
    "    risk_level TEXT DEFAULT 'info',"
    "    risk_reason TEXT"
    ");";

constexpr const char *kCreateSnapshotsTable =
    "CREATE TABLE IF NOT EXISTS snapshots ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    kernel_version TEXT NOT NULL,"
    "    gpu_driver TEXT,"
    "    firmware_versions TEXT,"
    "    key_packages TEXT"
    ");";

constexpr const char *kCreateMetaTable =
    "CREATE TABLE IF NOT EXISTS meta ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT NOT NULL"
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
    bool hasRiskColumns = false;
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

    impl->hasRiskColumns = columnExists(impl->db, "events", "risk_level");
    if (!impl->hasRiskColumns) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN risk_level TEXT DEFAULT 'info';");
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN risk_reason TEXT;");
            impl->hasRiskColumns = true;
        } catch (const std::exception &) {
            impl->hasRiskColumns = false;
        }
    } else {
        const bool hasReason = columnExists(impl->db, "events", "risk_reason");
        if (!hasReason) {
            try {
                execOrThrow(impl->db,
                            "ALTER TABLE events ADD COLUMN risk_reason TEXT;");
                impl->hasRiskColumns = true;
            } catch (const std::exception &) {
                impl->hasRiskColumns = false;
            }
        } else {
            impl->hasRiskColumns = true;
        }
    }

    if (!impl->hasRiskColumns) {
        std::cerr << "Khronicle: risk columns unavailable; defaulting risk metadata.\n";
    }
}

KhronicleStore::~KhronicleStore()
{
    if (impl && impl->db) {
        sqlite3_close(impl->db);
        impl->db = nullptr;
    }
}

void KhronicleStore::addEvent(const KhronicleEvent &event)
{
    Statement dedupeStmt(impl->db,
                         "SELECT 1 FROM events WHERE timestamp = ? AND category = ? "
                         "AND summary = ? LIMIT 1;");
    sqlite3_bind_int64(dedupeStmt.get(), 1, toEpochSeconds(event.timestamp));
    sqlite3_bind_int(dedupeStmt.get(), 2, static_cast<int>(event.category));
    bindText(dedupeStmt.get(), 3, event.summary);

    if (sqlite3_step(dedupeStmt.get()) == SQLITE_ROW) {
#ifndef NDEBUG
        std::cerr << "Khronicle: duplicate event suppressed.\n";
#endif
        return;
    }

    std::string sql =
        "INSERT OR REPLACE INTO events (id, timestamp, category, "
        "source, summary, details, before_state, after_state, "
        "related_packages";
    if (impl->hasRiskColumns) {
        sql += ", risk_level, risk_reason";
    }
    sql += ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?";
    if (impl->hasRiskColumns) {
        sql += ", ?, ?";
    }
    sql += ");";

    Statement stmt(impl->db, sql.c_str());
    bindText(stmt.get(), 1, event.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(event.timestamp));
    sqlite3_bind_int(stmt.get(), 3, static_cast<int>(event.category));
    sqlite3_bind_int(stmt.get(), 4, static_cast<int>(event.source));
    bindText(stmt.get(), 5, event.summary);
    bindOptionalText(stmt.get(), 6, event.details);
    bindJson(stmt.get(), 7, event.beforeState);
    bindJson(stmt.get(), 8, event.afterState);
    bindJson(stmt.get(), 9, nlohmann::json(event.relatedPackages));
    if (impl->hasRiskColumns) {
        bindText(stmt.get(), 10, event.riskLevel.empty() ? "info" : event.riskLevel);
        bindOptionalText(stmt.get(), 11, event.riskReason);
    }

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert event");
    }
}

void KhronicleStore::addSnapshot(const SystemSnapshot &snapshot)
{
    Statement stmt(impl->db,
                   "INSERT OR REPLACE INTO snapshots (id, timestamp, "
                   "kernel_version, gpu_driver, firmware_versions, "
                   "key_packages) VALUES (?, ?, ?, ?, ?, ?);");
    bindText(stmt.get(), 1, snapshot.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(snapshot.timestamp));
    bindText(stmt.get(), 3, snapshot.kernelVersion);
    bindJson(stmt.get(), 4, snapshot.gpuDriver);
    bindJson(stmt.get(), 5, snapshot.firmwareVersions);
    bindJson(stmt.get(), 6, snapshot.keyPackages);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert snapshot");
    }
}

std::vector<KhronicleEvent> KhronicleStore::getEventsSince(
    std::chrono::system_clock::time_point since) const
{
    std::string sql =
        "SELECT id, timestamp, category, source, summary, details, "
        "before_state, after_state, related_packages";
    if (impl->hasRiskColumns) {
        sql += ", risk_level, risk_reason";
    }
    sql += " FROM events WHERE timestamp >= ? ORDER BY timestamp ASC;";
    Statement stmt(impl->db, sql.c_str());
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
        if (impl->hasRiskColumns) {
            event.riskLevel = columnText(stmt.get(), 9);
            event.riskReason = columnText(stmt.get(), 10);
            if (event.riskLevel.empty()) {
                event.riskLevel = "info";
            }
        } else {
            event.riskLevel = "info";
            event.riskReason.clear();
        }
        events.push_back(std::move(event));
    }

    return events;
}

std::vector<KhronicleEvent> KhronicleStore::getEventsBetween(
    std::chrono::system_clock::time_point from,
    std::chrono::system_clock::time_point to) const
{
    std::string sql =
        "SELECT id, timestamp, category, source, summary, details, "
        "before_state, after_state, related_packages";
    if (impl->hasRiskColumns) {
        sql += ", risk_level, risk_reason";
    }
    sql += " FROM events WHERE timestamp >= ? AND timestamp <= ? "
           "ORDER BY timestamp ASC;";
    Statement stmt(impl->db, sql.c_str());
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
        if (impl->hasRiskColumns) {
            event.riskLevel = columnText(stmt.get(), 9);
            event.riskReason = columnText(stmt.get(), 10);
            if (event.riskLevel.empty()) {
                event.riskLevel = "info";
            }
        } else {
            event.riskLevel = "info";
            event.riskReason.clear();
        }
        events.push_back(std::move(event));
    }

    return events;
}

std::vector<SystemSnapshot> KhronicleStore::listSnapshots() const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages "
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
        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

std::optional<SystemSnapshot> KhronicleStore::getSnapshot(
    const std::string &id) const
{
    Statement stmt(impl->db,
                   "SELECT id, timestamp, kernel_version, gpu_driver, "
                   "firmware_versions, key_packages "
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

bool KhronicleStore::integrityCheck(std::string *message) const
{
    Statement stmt(impl->db, "PRAGMA integrity_check;");

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        if (message) {
            *message = "integrity_check failed to return a result";
        }
        return false;
    }

    const std::string result = columnText(stmt.get(), 0);
    if (message) {
        *message = result;
    }
    return result == "ok";
}

} // namespace khronicle
