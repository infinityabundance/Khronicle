#include "daemon/khronicle_store.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
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
    "    risk_reason TEXT,"
    "    provenance TEXT"
    ");";

constexpr const char *kCreateSnapshotsTable =
    "CREATE TABLE IF NOT EXISTS snapshots ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    kernel_version TEXT NOT NULL,"
    "    gpu_driver TEXT,"
    "    firmware_versions TEXT,"
    "    key_packages TEXT,"
    "    snapshot_id TEXT,"
    "    ingestion_id TEXT,"
    "    kernel_source TEXT"
    ");";

constexpr const char *kCreateMetaTable =
    "CREATE TABLE IF NOT EXISTS meta ("
    "    key TEXT PRIMARY KEY,"
    "    value TEXT NOT NULL"
    ");";

constexpr const char *kCreateAuditLogTable =
    "CREATE TABLE IF NOT EXISTS audit_log ("
    "    id TEXT PRIMARY KEY,"
    "    timestamp INTEGER NOT NULL,"
    "    audit_type TEXT NOT NULL,"
    "    input_refs TEXT NOT NULL,"
    "    method TEXT NOT NULL,"
    "    output_summary TEXT"
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
    bool hasProvenanceColumn = false;
    bool hasSnapshotIdColumn = false;
    bool hasSnapshotIngestionColumn = false;
    bool hasKernelSourceColumn = false;
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
    execOrThrow(impl->db, kCreateAuditLogTable);

    impl->hasRiskColumns = columnExists(impl->db, "events", "risk_level");
    if (!impl->hasRiskColumns) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN risk_level TEXT DEFAULT 'info';");
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN risk_reason TEXT;");
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN provenance TEXT;");
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

    impl->hasProvenanceColumn = columnExists(impl->db, "events", "provenance");
    if (!impl->hasProvenanceColumn) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE events ADD COLUMN provenance TEXT;");
            impl->hasProvenanceColumn = true;
        } catch (const std::exception &) {
            impl->hasProvenanceColumn = false;
        }
    }

    impl->hasSnapshotIdColumn = columnExists(impl->db, "snapshots", "snapshot_id");
    if (!impl->hasSnapshotIdColumn) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE snapshots ADD COLUMN snapshot_id TEXT;");
            impl->hasSnapshotIdColumn = true;
        } catch (const std::exception &) {
            impl->hasSnapshotIdColumn = false;
        }
    }

    impl->hasSnapshotIngestionColumn = columnExists(impl->db, "snapshots", "ingestion_id");
    if (!impl->hasSnapshotIngestionColumn) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE snapshots ADD COLUMN ingestion_id TEXT;");
            impl->hasSnapshotIngestionColumn = true;
        } catch (const std::exception &) {
            impl->hasSnapshotIngestionColumn = false;
        }
    }

    impl->hasKernelSourceColumn = columnExists(impl->db, "snapshots", "kernel_source");
    if (!impl->hasKernelSourceColumn) {
        try {
            execOrThrow(impl->db,
                        "ALTER TABLE snapshots ADD COLUMN kernel_source TEXT;");
            impl->hasKernelSourceColumn = true;
        } catch (const std::exception &) {
            impl->hasKernelSourceColumn = false;
        }
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
    if (impl->hasProvenanceColumn) {
        sql += ", provenance";
    }
    sql += ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?";
    if (impl->hasRiskColumns) {
        sql += ", ?, ?";
    }
    if (impl->hasProvenanceColumn) {
        sql += ", ?";
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
    int bindIndex = 10;
    if (impl->hasRiskColumns) {
        bindText(stmt.get(), bindIndex++, event.riskLevel.empty() ? "info" : event.riskLevel);
        bindOptionalText(stmt.get(), bindIndex++, event.riskReason);
    }
    if (impl->hasProvenanceColumn) {
        nlohmann::json prov{
            {"sourceType", event.provenance.sourceType},
            {"sourceRef", event.provenance.sourceRef},
            {"parserVersion", event.provenance.parserVersion},
            {"ingestionId", event.provenance.ingestionId}
        };
        bindOptionalText(stmt.get(), bindIndex++, prov.dump());
    }

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert event");
    }
}

void KhronicleStore::addSnapshot(const SystemSnapshot &snapshot)
{
    std::string sql =
        "INSERT OR REPLACE INTO snapshots (id, timestamp, "
        "kernel_version, gpu_driver, firmware_versions, "
        "key_packages";
    if (impl->hasSnapshotIdColumn) {
        sql += ", snapshot_id";
    }
    if (impl->hasSnapshotIngestionColumn) {
        sql += ", ingestion_id";
    }
    if (impl->hasKernelSourceColumn) {
        sql += ", kernel_source";
    }
    sql += ") VALUES (?, ?, ?, ?, ?, ?";
    if (impl->hasSnapshotIdColumn) {
        sql += ", ?";
    }
    if (impl->hasSnapshotIngestionColumn) {
        sql += ", ?";
    }
    if (impl->hasKernelSourceColumn) {
        sql += ", ?";
    }
    sql += ");";

    Statement stmt(impl->db, sql.c_str());
    bindText(stmt.get(), 1, snapshot.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(snapshot.timestamp));
    bindText(stmt.get(), 3, snapshot.kernelVersion);
    bindJson(stmt.get(), 4, snapshot.gpuDriver);
    bindJson(stmt.get(), 5, snapshot.firmwareVersions);
    bindJson(stmt.get(), 6, snapshot.keyPackages);
    int bindIndex = 7;
    if (impl->hasSnapshotIdColumn) {
        bindOptionalText(stmt.get(), bindIndex++, snapshot.snapshotId);
    }
    if (impl->hasSnapshotIngestionColumn) {
        bindOptionalText(stmt.get(), bindIndex++, snapshot.ingestionId);
    }
    if (impl->hasKernelSourceColumn) {
        bindOptionalText(stmt.get(), bindIndex++, snapshot.kernelSource);
    }

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
    if (impl->hasProvenanceColumn) {
        sql += ", provenance";
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
        int colIndex = 9;
        if (impl->hasRiskColumns) {
            event.riskLevel = columnText(stmt.get(), colIndex++);
            event.riskReason = columnText(stmt.get(), colIndex++);
            if (event.riskLevel.empty()) {
                event.riskLevel = "info";
            }
        } else {
            event.riskLevel = "info";
            event.riskReason.clear();
        }
        if (impl->hasProvenanceColumn) {
            const std::string provText = columnText(stmt.get(), colIndex++);
            if (!provText.empty()) {
                try {
                    const auto provJson = nlohmann::json::parse(provText);
                    event.provenance.sourceType = provJson.value("sourceType", "unknown");
                    event.provenance.sourceRef = provJson.value("sourceRef", "");
                    event.provenance.parserVersion = provJson.value("parserVersion", "legacy");
                    event.provenance.ingestionId = provJson.value("ingestionId", "");
                } catch (const nlohmann::json::parse_error &) {
                    event.provenance.sourceType = "unknown";
                    event.provenance.parserVersion = "legacy";
                }
            } else {
                event.provenance.sourceType = "unknown";
                event.provenance.parserVersion = "legacy";
            }
        } else {
            event.provenance.sourceType = "unknown";
            event.provenance.parserVersion = "legacy";
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
    if (impl->hasProvenanceColumn) {
        sql += ", provenance";
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
        int colIndex = 9;
        if (impl->hasRiskColumns) {
            event.riskLevel = columnText(stmt.get(), colIndex++);
            event.riskReason = columnText(stmt.get(), colIndex++);
            if (event.riskLevel.empty()) {
                event.riskLevel = "info";
            }
        } else {
            event.riskLevel = "info";
            event.riskReason.clear();
        }
        if (impl->hasProvenanceColumn) {
            const std::string provText = columnText(stmt.get(), colIndex++);
            if (!provText.empty()) {
                try {
                    const auto provJson = nlohmann::json::parse(provText);
                    event.provenance.sourceType = provJson.value("sourceType", "unknown");
                    event.provenance.sourceRef = provJson.value("sourceRef", "");
                    event.provenance.parserVersion = provJson.value("parserVersion", "legacy");
                    event.provenance.ingestionId = provJson.value("ingestionId", "");
                } catch (const nlohmann::json::parse_error &) {
                    event.provenance.sourceType = "unknown";
                    event.provenance.parserVersion = "legacy";
                }
            } else {
                event.provenance.sourceType = "unknown";
                event.provenance.parserVersion = "legacy";
            }
        } else {
            event.provenance.sourceType = "unknown";
            event.provenance.parserVersion = "legacy";
        }
        events.push_back(std::move(event));
    }

    return events;
}

std::vector<SystemSnapshot> KhronicleStore::listSnapshots() const
{
    std::string sql =
        "SELECT id, timestamp, kernel_version, gpu_driver, "
        "firmware_versions, key_packages";
    if (impl->hasSnapshotIdColumn) {
        sql += ", snapshot_id";
    }
    if (impl->hasSnapshotIngestionColumn) {
        sql += ", ingestion_id";
    }
    if (impl->hasKernelSourceColumn) {
        sql += ", kernel_source";
    }
    sql += " FROM snapshots ORDER BY timestamp ASC;";
    Statement stmt(impl->db, sql.c_str());

    std::vector<SystemSnapshot> snapshots;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        SystemSnapshot snapshot;
        snapshot.id = columnText(stmt.get(), 0);
        snapshot.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
        snapshot.kernelVersion = columnText(stmt.get(), 2);
        snapshot.gpuDriver = columnJson(stmt.get(), 3);
        snapshot.firmwareVersions = columnJson(stmt.get(), 4);
        snapshot.keyPackages = columnJson(stmt.get(), 5);
        int colIndex = 6;
        if (impl->hasSnapshotIdColumn) {
            snapshot.snapshotId = columnText(stmt.get(), colIndex++);
        } else {
            snapshot.snapshotId = snapshot.id;
        }
        if (impl->hasSnapshotIngestionColumn) {
            snapshot.ingestionId = columnText(stmt.get(), colIndex++);
        } else {
            snapshot.ingestionId.clear();
        }
        if (impl->hasKernelSourceColumn) {
            snapshot.kernelSource = columnText(stmt.get(), colIndex++);
        } else {
            snapshot.kernelSource.clear();
        }
        snapshots.push_back(std::move(snapshot));
    }

    return snapshots;
}

std::optional<SystemSnapshot> KhronicleStore::getSnapshot(
    const std::string &id) const
{
    std::string sql =
        "SELECT id, timestamp, kernel_version, gpu_driver, "
        "firmware_versions, key_packages";
    if (impl->hasSnapshotIdColumn) {
        sql += ", snapshot_id";
    }
    if (impl->hasSnapshotIngestionColumn) {
        sql += ", ingestion_id";
    }
    if (impl->hasKernelSourceColumn) {
        sql += ", kernel_source";
    }
    sql += " FROM snapshots WHERE id = ? LIMIT 1;";
    Statement stmt(impl->db, sql.c_str());
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
    int colIndex = 6;
    if (impl->hasSnapshotIdColumn) {
        snapshot.snapshotId = columnText(stmt.get(), colIndex++);
    } else {
        snapshot.snapshotId = snapshot.id;
    }
    if (impl->hasSnapshotIngestionColumn) {
        snapshot.ingestionId = columnText(stmt.get(), colIndex++);
    } else {
        snapshot.ingestionId.clear();
    }
    if (impl->hasKernelSourceColumn) {
        snapshot.kernelSource = columnText(stmt.get(), colIndex++);
    } else {
        snapshot.kernelSource.clear();
    }

    return snapshot;
}

KhronicleDiff KhronicleStore::diffSnapshots(const std::string &aId,
                                            const std::string &bId)
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

    AuditLogEntry audit;
    audit.id = generateUuid();
    audit.timestamp = std::chrono::system_clock::now();
    audit.auditType = "snapshot_diff";
    audit.inputRefs = {aId, bId};
    audit.method = "SnapshotDiffer@1";
    audit.outputSummary = "Changed fields: " + std::to_string(diff.changedFields.size());
    try {
        addAuditLog(audit);
    } catch (const std::exception &) {
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

std::optional<nlohmann::json> KhronicleStore::getEventProvenance(
    const std::string &id) const
{
    if (!impl->hasProvenanceColumn) {
        return nlohmann::json{
            {"sourceType", "unknown"},
            {"sourceRef", ""},
            {"parserVersion", "legacy"},
            {"ingestionId", ""}
        };
    }

    Statement stmt(impl->db,
                   "SELECT provenance FROM events WHERE id = ? LIMIT 1;");
    bindText(stmt.get(), 1, id);

    if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
        return std::nullopt;
    }

    const std::string provText = columnText(stmt.get(), 0);
    if (provText.empty()) {
        return nlohmann::json::object();
    }

    try {
        return nlohmann::json::parse(provText);
    } catch (const nlohmann::json::parse_error &) {
        return nlohmann::json::object();
    }
}

void KhronicleStore::addAuditLog(const AuditLogEntry &entry)
{
    Statement stmt(impl->db,
                   "INSERT OR REPLACE INTO audit_log (id, timestamp, audit_type, "
                   "input_refs, method, output_summary) VALUES (?, ?, ?, ?, ?, ?);");
    bindText(stmt.get(), 1, entry.id);
    sqlite3_bind_int64(stmt.get(), 2, toEpochSeconds(entry.timestamp));
    bindText(stmt.get(), 3, entry.auditType);
    bindText(stmt.get(), 4, nlohmann::json(entry.inputRefs).dump());
    bindText(stmt.get(), 5, entry.method);
    bindOptionalText(stmt.get(), 6, entry.outputSummary);

    if (sqlite3_step(stmt.get()) != SQLITE_DONE) {
        throw std::runtime_error("failed to insert audit log");
    }
}

void KhronicleStore::addRiskAuditIfNeeded(const KhronicleEvent &event)
{
    if (event.riskLevel != "important" && event.riskLevel != "critical") {
        return;
    }

    AuditLogEntry entry;
    entry.id = generateUuid();
    entry.timestamp = std::chrono::system_clock::now();
    entry.auditType = "risk_classification";
    entry.inputRefs = {event.id};
    entry.method = "RiskClassifier@1";
    entry.outputSummary = event.riskReason.empty()
        ? event.summary
        : event.riskReason;

    addAuditLog(entry);
}

std::vector<AuditLogEntry> KhronicleStore::getAuditLogSince(
    std::chrono::system_clock::time_point since,
    const std::optional<std::string> &type) const
{
    std::string sql =
        "SELECT id, timestamp, audit_type, input_refs, method, output_summary "
        "FROM audit_log WHERE timestamp >= ?";
    if (type.has_value()) {
        sql += " AND audit_type = ?";
    }
    sql += " ORDER BY timestamp ASC;";

    Statement stmt(impl->db, sql.c_str());
    sqlite3_bind_int64(stmt.get(), 1, toEpochSeconds(since));
    if (type.has_value()) {
        bindText(stmt.get(), 2, *type);
    }

    std::vector<AuditLogEntry> entries;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        AuditLogEntry entry;
        entry.id = columnText(stmt.get(), 0);
        entry.timestamp = fromEpochSeconds(sqlite3_column_int64(stmt.get(), 1));
        entry.auditType = columnText(stmt.get(), 2);
        const std::string refsText = columnText(stmt.get(), 3);
        if (!refsText.empty()) {
            try {
                auto refsJson = nlohmann::json::parse(refsText);
                if (refsJson.is_array()) {
                    entry.inputRefs = refsJson.get<std::vector<std::string>>();
                }
            } catch (const nlohmann::json::parse_error &) {
                entry.inputRefs.clear();
            }
        }
        entry.method = columnText(stmt.get(), 4);
        entry.outputSummary = columnText(stmt.get(), 5);
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::string KhronicleStore::schemaSql() const
{
    Statement stmt(impl->db,
                   "SELECT sql FROM sqlite_master WHERE sql IS NOT NULL "
                   "AND type IN ('table','index','trigger') ORDER BY name;");
    std::string schema;
    while (sqlite3_step(stmt.get()) == SQLITE_ROW) {
        const std::string sql = columnText(stmt.get(), 0);
        if (!sql.empty()) {
            schema += sql;
            schema += "\n";
        }
    }
    return schema;
}

} // namespace khronicle
