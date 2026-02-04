#include "daemon/khronicle_daemon.hpp"

#include <algorithm>
#include <chrono>

#include <QTimer>

#include "daemon/khronicle_api_server.hpp"
#include "daemon/journal_parser.hpp"
#include "daemon/pacman_parser.hpp"
#include "daemon/snapshot_builder.hpp"
#include "daemon/watch_engine.hpp"
#include "common/json_utils.hpp"
#include "common/logging.hpp"
#include "debug/scenario_capture.hpp"

#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

constexpr int kIngestionIntervalMs = 300000;

std::chrono::system_clock::time_point defaultJournalStart()
{
    return std::chrono::system_clock::now() - std::chrono::minutes(30);
}

std::string timePointToIso(std::chrono::system_clock::time_point time)
{
    return toIso8601Utc(time);
}

std::chrono::system_clock::time_point isoToTimePoint(const std::string &value)
{
    auto parsed = fromIso8601Utc(value);
    if (parsed == std::chrono::system_clock::time_point{}) {
        return defaultJournalStart();
    }
    return parsed;
}

std::string detectKernelPackage(const SystemSnapshot &snapshot)
{
    const std::vector<std::string> kernelPackages = {
        "linux-cachyos",
        "linux",
        "linux-zen",
        "linux-lts",
    };

    for (const auto &name : kernelPackages) {
        if (snapshot.keyPackages.contains(name)) {
            return name;
        }
    }
    return "linux";
}

} // namespace

KhronicleDaemon::KhronicleDaemon(QObject *parent)
    : QObject(parent)
    , m_store(std::make_unique<KhronicleStore>())
    , m_journalLastTimestamp(defaultJournalStart())
{
    m_watchEngine = std::make_unique<WatchEngine>(*m_store);
    loadStateFromMeta();
    loadLastSnapshotFromStore();
}

KhronicleDaemon::~KhronicleDaemon() = default;

void KhronicleDaemon::start()
{
    if (!m_apiServer) {
        m_apiServer = std::make_unique<KhronicleApiServer>(*m_store);
        m_apiServer->start();
    }

    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("start"),
              QStringLiteral("daemon_start"),
              QStringLiteral("user_start"),
              QStringLiteral("timer_loop"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"intervalMs", kIngestionIntervalMs}}));

    // Timer-driven ingestion loop: keep work bounded and predictable.
    auto *timer = new QTimer(this);
    timer->setInterval(kIngestionIntervalMs);
    connect(timer, &QTimer::timeout, this, &KhronicleDaemon::runIngestionCycle);
    timer->start();

    runIngestionCycle();
}

void KhronicleDaemon::runIngestionCycle()
{
    // One ingestion cycle:
    // 1) pacman log ingestion
    // 2) journal ingestion
    // 3) snapshot check + optional event emission
    // 4) persist resume state to the meta table
    const auto cycleStart = std::chrono::steady_clock::now();
    static uint64_t cycleIndex = 0;
    const QString corrId = QStringLiteral("ingestion-%1").arg(++cycleIndex);
    khronicle::logging::CorrelationScope corrScope(corrId);
    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("runIngestionCycle"),
              QStringLiteral("start_ingestion_cycle"),
              QStringLiteral("timer_tick"),
              QStringLiteral("bounded_batch"),
              khronicle::logging::defaultWho(),
              corrId,
              (nlohmann::json{{"cycleIndex", cycleIndex}}));

    if (ScenarioCapture::isEnabled()) {
        ScenarioCapture::recordStep(nlohmann::json{
            {"action", "run_ingestion_cycle"},
            {"context", {{"cycleIndex", cycleIndex}}}
        });
    }

    runPacmanIngestion();
    runJournalIngestion();
    runSnapshotCheck();
    persistStateToMeta();

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - cycleStart).count();
    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("runIngestionCycle"),
              QStringLiteral("end_ingestion_cycle"),
              QStringLiteral("timer_tick"),
              QStringLiteral("bounded_batch"),
              khronicle::logging::defaultWho(),
              corrId,
              (nlohmann::json{{"durationMs", elapsedMs}}));
}

void KhronicleDaemon::runIngestionCycleForReplay()
{
    runIngestionCycle();
}

void KhronicleDaemon::runPacmanIngestion()
{
    // Parse new pacman log entries from the last cursor.
    const char *overridePath = std::getenv("KHRONICLE_PACMAN_LOG_PATH");
    const std::string logPath = overridePath ? overridePath : "/var/log/pacman.log";
    KLOG_DEBUG(QStringLiteral("KhronicleDaemon"),
               QStringLiteral("runPacmanIngestion"),
               QStringLiteral("ingest_pacman_start"),
               QStringLiteral("ingestion_cycle"),
               QStringLiteral("parse_log"),
               khronicle::logging::defaultWho(),
               QString(),
               (nlohmann::json{{"cursor", m_pacmanCursor.value_or("")},
                              {"path", logPath}}));
    const PacmanParseResult result =
        parsePacmanLog(logPath, m_pacmanCursor);

    const std::string hostId = m_store->getHostIdentity().hostId;
    size_t ingested = 0;
    for (auto event : result.events) {
        event.hostId = hostId;
        m_store->addEvent(event);
        if (m_watchEngine) {
            m_watchEngine->evaluateEvent(event);
        }
        ingested++;
    }

    if (!result.newCursor.empty()) {
        m_pacmanCursor = result.newCursor;
    }

    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("runPacmanIngestion"),
              QStringLiteral("ingest_pacman_complete"),
              QStringLiteral("ingestion_cycle"),
              QStringLiteral("parse_log"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"events", ingested},
                             {"newCursor", result.newCursor}}));
}

void KhronicleDaemon::runJournalIngestion()
{
    // Parse journal entries since the last observed timestamp.
    KLOG_DEBUG(QStringLiteral("KhronicleDaemon"),
               QStringLiteral("runJournalIngestion"),
               QStringLiteral("ingest_journal_start"),
               QStringLiteral("ingestion_cycle"),
               QStringLiteral("journalctl"),
               khronicle::logging::defaultWho(),
               QString(),
               (nlohmann::json{{"since", toIso8601Utc(m_journalLastTimestamp)}}));
    const JournalParseResult result = parseJournalSince(m_journalLastTimestamp);

    const std::string hostId = m_store->getHostIdentity().hostId;
    size_t ingested = 0;
    for (auto event : result.events) {
        event.hostId = hostId;
        m_store->addEvent(event);
        if (m_watchEngine) {
            m_watchEngine->evaluateEvent(event);
        }
        ingested++;
    }

    if (result.lastTimestamp > m_journalLastTimestamp) {
        m_journalLastTimestamp = result.lastTimestamp;
    }

    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("runJournalIngestion"),
              QStringLiteral("ingest_journal_complete"),
              QStringLiteral("ingestion_cycle"),
              QStringLiteral("journalctl"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"events", ingested},
                             {"lastTimestamp", toIso8601Utc(result.lastTimestamp)}}));
}

void KhronicleDaemon::runSnapshotCheck()
{
    // Snapshot builder captures point-in-time system state. We only write a new
    // snapshot when kernel changes (current heuristic).
    if (qEnvironmentVariableIntValue("KHRONICLE_REPLAY_NO_SNAPSHOT") == 1) {
        KLOG_INFO(QStringLiteral("KhronicleDaemon"),
                  QStringLiteral("runSnapshotCheck"),
                  QStringLiteral("snapshot_skipped"),
                  QStringLiteral("replay_mode"),
                  QStringLiteral("skip_snapshot"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json::object());
        return;
    }
    KLOG_DEBUG(QStringLiteral("KhronicleDaemon"),
               QStringLiteral("runSnapshotCheck"),
               QStringLiteral("snapshot_check_start"),
               QStringLiteral("ingestion_cycle"),
               QStringLiteral("kernel_change_heuristic"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json::object());
    SystemSnapshot current = buildCurrentSnapshot();
    current.hostIdentity = m_store->getHostIdentity();

    if (!m_lastSnapshot.has_value()) {
        m_store->addSnapshot(current);
        if (m_watchEngine) {
            m_watchEngine->evaluateSnapshot(current);
        }
        m_lastSnapshot = current;
        KLOG_INFO(QStringLiteral("KhronicleDaemon"),
                  QStringLiteral("runSnapshotCheck"),
                  QStringLiteral("snapshot_inserted"),
                  QStringLiteral("initial_snapshot"),
                  QStringLiteral("kernel_change_heuristic"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  (nlohmann::json{{"snapshotId", current.id}}));
        return;
    }

    if (m_lastSnapshot->kernelVersion == current.kernelVersion) {
        KLOG_DEBUG(QStringLiteral("KhronicleDaemon"),
                   QStringLiteral("runSnapshotCheck"),
                   QStringLiteral("snapshot_skipped"),
                   QStringLiteral("kernel_unchanged"),
                   QStringLiteral("kernel_change_heuristic"),
                   khronicle::logging::defaultWho(),
                   QString(),
                   (nlohmann::json{{"kernelVersion", current.kernelVersion}}));
        return;
    }

    m_store->addSnapshot(current);
    if (m_watchEngine) {
        m_watchEngine->evaluateSnapshot(current);
    }

    KhronicleEvent event;
    event.id = "kernel-change-"
        + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                             current.timestamp.time_since_epoch())
                             .count());
    event.timestamp = current.timestamp;
    event.category = EventCategory::Kernel;
    event.source = EventSource::Uname;
    event.summary = "Kernel changed: " + m_lastSnapshot->kernelVersion + " -> "
        + current.kernelVersion;
    event.details = "Kernel version changed from " + m_lastSnapshot->kernelVersion
        + " to " + current.kernelVersion;
    event.beforeState = nlohmann::json::object();
    event.afterState = nlohmann::json::object();
    event.beforeState["kernelVersion"] = m_lastSnapshot->kernelVersion;
    event.afterState["kernelVersion"] = current.kernelVersion;
    event.relatedPackages = {detectKernelPackage(current)};
    event.hostId = current.hostIdentity.hostId;

    m_store->addEvent(event);
    if (m_watchEngine) {
        m_watchEngine->evaluateEvent(event);
    }
    KLOG_INFO(QStringLiteral("KhronicleDaemon"),
              QStringLiteral("runSnapshotCheck"),
              QStringLiteral("snapshot_inserted"),
              QStringLiteral("kernel_changed"),
              QStringLiteral("kernel_change_heuristic"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"snapshotId", current.id},
                             {"kernelFrom", m_lastSnapshot->kernelVersion},
                             {"kernelTo", current.kernelVersion}}));
    m_lastSnapshot = current;
}

void KhronicleDaemon::loadStateFromMeta()
{
    if (const auto pacmanCursor = m_store->getMeta("pacman_last_cursor")) {
        m_pacmanCursor = *pacmanCursor;
    }

    if (const auto journalTimestamp =
            m_store->getMeta("journal_last_timestamp")) {
        m_journalLastTimestamp = isoToTimePoint(*journalTimestamp);
    }
}

void KhronicleDaemon::persistStateToMeta()
{
    if (m_pacmanCursor.has_value()) {
        m_store->setMeta("pacman_last_cursor", *m_pacmanCursor);
    }

    m_store->setMeta("journal_last_timestamp",
                     timePointToIso(m_journalLastTimestamp));
}

void KhronicleDaemon::loadLastSnapshotFromStore()
{
    const auto snapshots = m_store->listSnapshots();
    if (snapshots.empty()) {
        return;
    }

    auto latest = std::max_element(
        snapshots.begin(), snapshots.end(),
        [](const SystemSnapshot &a, const SystemSnapshot &b) {
            return a.timestamp < b.timestamp;
        });

    if (latest != snapshots.end()) {
        m_lastSnapshot = *latest;
    }
}

} // namespace khronicle
