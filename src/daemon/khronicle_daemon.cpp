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

    auto *timer = new QTimer(this);
    timer->setInterval(kIngestionIntervalMs);
    connect(timer, &QTimer::timeout, this, &KhronicleDaemon::runIngestionCycle);
    timer->start();

    runIngestionCycle();
}

void KhronicleDaemon::runIngestionCycle()
{
    runPacmanIngestion();
    runJournalIngestion();
    runSnapshotCheck();
    persistStateToMeta();
}

void KhronicleDaemon::runPacmanIngestion()
{
    const PacmanParseResult result =
        parsePacmanLog("/var/log/pacman.log", m_pacmanCursor);

    const std::string hostId = m_store->getHostIdentity().hostId;
    for (auto event : result.events) {
        event.hostId = hostId;
        m_store->addEvent(event);
        if (m_watchEngine) {
            m_watchEngine->evaluateEvent(event);
        }
    }

    if (!result.newCursor.empty()) {
        m_pacmanCursor = result.newCursor;
    }
}

void KhronicleDaemon::runJournalIngestion()
{
    const JournalParseResult result = parseJournalSince(m_journalLastTimestamp);

    const std::string hostId = m_store->getHostIdentity().hostId;
    for (auto event : result.events) {
        event.hostId = hostId;
        m_store->addEvent(event);
        if (m_watchEngine) {
            m_watchEngine->evaluateEvent(event);
        }
    }

    if (result.lastTimestamp > m_journalLastTimestamp) {
        m_journalLastTimestamp = result.lastTimestamp;
    }
}

void KhronicleDaemon::runSnapshotCheck()
{
    SystemSnapshot current = buildCurrentSnapshot();
    current.hostIdentity = m_store->getHostIdentity();

    if (!m_lastSnapshot.has_value()) {
        m_store->addSnapshot(current);
        if (m_watchEngine) {
            m_watchEngine->evaluateSnapshot(current);
        }
        m_lastSnapshot = current;
        return;
    }

    if (m_lastSnapshot->kernelVersion == current.kernelVersion) {
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
