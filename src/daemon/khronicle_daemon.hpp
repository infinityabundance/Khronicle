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

/**
 * KhronicleDaemon coordinates:
 * - periodic parsing of pacman.log and systemd journal
 * - building system snapshots
 * - persisting events and snapshots into KhronicleStore
 *
 * It is designed to be owned from main() and driven by Qt's event loop.
 */
class KhronicleDaemon : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleDaemon(QObject *parent = nullptr);
    ~KhronicleDaemon() override;

    // Call this after constructing the daemon to set up timers and start periodic work.
    void start();

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
    bool m_ingestionEnabled = true;
    std::string m_currentIngestionId;

    // In-memory cached state for faster access between cycles.
    std::optional<std::string> m_pacmanCursor;
    std::chrono::system_clock::time_point m_journalLastTimestamp;
    std::optional<SystemSnapshot> m_lastSnapshot;

    int m_pacmanErrorCount = 0;
    int m_journalErrorCount = 0;
    int m_pacmanBackoffCycles = 0;
    int m_journalBackoffCycles = 0;
};

} // namespace khronicle
