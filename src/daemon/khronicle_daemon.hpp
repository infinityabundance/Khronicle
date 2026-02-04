#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <QObject>

#include "daemon/khronicle_store.hpp"
#include "common/models.hpp"

namespace khronicle {

class KhronicleApiServer;
class WatchEngine;

/**
 * KhronicleDaemon orchestrates ingestion and API serving for a single host.
 *
 * Responsibilities:
 * - Periodically ingest new pacman and journal entries.
 * - Build and persist system snapshots in SQLite.
 * - Evaluate watch rules and record watch signals.
 * - Serve a local JSON-RPC API for UI and tools.
 *
 * This class is owned by main() in khronicle-daemon and lives for the
 * lifetime of the daemon process.
 */
class KhronicleDaemon : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleDaemon(QObject *parent = nullptr);
    ~KhronicleDaemon() override;

    // Call this after constructing the daemon to set up timers and start periodic work.
    void start();
    void runIngestionCycleForReplay();

private slots:
    void runIngestionCycle();

private:
    void runPacmanIngestion();
    void runJournalIngestion();
    void runSnapshotCheck();

    void loadStateFromMeta();
    void persistStateToMeta();

    void loadLastSnapshotFromStore();

    std::unique_ptr<KhronicleStore> m_store;
    std::unique_ptr<KhronicleApiServer> m_apiServer;
    std::unique_ptr<WatchEngine> m_watchEngine;

    // In-memory cached state for faster access between cycles.
    std::optional<std::string> m_pacmanCursor;
    std::chrono::system_clock::time_point m_journalLastTimestamp;
    std::optional<SystemSnapshot> m_lastSnapshot;
};

} // namespace khronicle
