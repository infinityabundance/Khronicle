#include "daemon/khronicle_daemon.hpp"

#include <algorithm>
#include <chrono>

#include <QTimer>
#include <QDebug>

#include "daemon/khronicle_api_server.hpp"
#include "daemon/journal_parser.hpp"
#include "daemon/pacman_parser.hpp"
#include "daemon/risk_classifier.hpp"
#include "daemon/snapshot_builder.hpp"
#include "common/json_utils.hpp"
#include "common/khronicle_version.hpp"

#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

constexpr int kIngestionIntervalMs = 300000;
constexpr auto kMaxCycleDuration = std::chrono::milliseconds(2000);
constexpr auto kMaxJournalLookback = std::chrono::hours(24 * 7);
constexpr int kMaxConsecutiveErrors = 3;
constexpr int kBackoffCycles = 2;

std::chrono::system_clock::time_point defaultJournalStart()
{
    return std::chrono::system_clock::now() - std::chrono::minutes(30);
}

std::string timePointToIso(std::chrono::system_clock::time_point time)
{
    return toIso8601Utc(time);
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
    std::string integrityMessage;
    if (!m_store->integrityCheck(&integrityMessage)) {
        qWarning() << "Khronicle: SQLite integrity check failed — database may be corrupt:"
                   << QString::fromStdString(integrityMessage);
        m_ingestionEnabled = false;
    }

    loadStateFromMeta();
    loadLastSnapshotFromStore();
}

KhronicleDaemon::~KhronicleDaemon() = default;

void KhronicleDaemon::start()
{
    qInfo() << "Khronicle: daemon starting (version" << KHRONICLE_VERSION << ")";

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
    if (!m_ingestionEnabled) {
        return;
    }

    const auto cycleStart = std::chrono::steady_clock::now();

    runPacmanIngestion();
    if (std::chrono::steady_clock::now() - cycleStart > kMaxCycleDuration) {
        qWarning() << "Khronicle: Ingestion cycle exceeded time budget, deferring remaining work.";
        persistStateToMeta();
        return;
    }

    runJournalIngestion();
    if (std::chrono::steady_clock::now() - cycleStart > kMaxCycleDuration) {
        qWarning() << "Khronicle: Ingestion cycle exceeded time budget, deferring remaining work.";
        persistStateToMeta();
        return;
    }

    runSnapshotCheck();
    persistStateToMeta();
}

void KhronicleDaemon::runPacmanIngestion()
{
    if (m_pacmanBackoffCycles > 0) {
        --m_pacmanBackoffCycles;
        return;
    }

    const PacmanParseResult result =
        parsePacmanLog("/var/log/pacman.log", m_pacmanCursor);

    for (auto event : result.events) {
        RiskClassifier::classify(event);
        m_store->addEvent(event);
    }

    if (!result.newCursor.empty()) {
        m_pacmanCursor = result.newCursor;
    }

#ifndef NDEBUG
    if (m_pacmanCursor.has_value()) {
        try {
            const long long value = std::stoll(*m_pacmanCursor);
            Q_ASSERT(value >= 0);
        } catch (const std::exception &) {
            Q_ASSERT(false);
        }
    }
#endif

    if (result.hadError) {
        if (m_pacmanErrorCount == 0) {
            qWarning() << "Khronicle: pacman ingestion failed; pacman.log may be unreadable.";
        }
        m_pacmanErrorCount++;
        if (m_pacmanErrorCount >= kMaxConsecutiveErrors) {
            qWarning() << "Khronicle: pacman ingestion failed repeatedly, backing off.";
            m_pacmanBackoffCycles = kBackoffCycles;
            m_pacmanErrorCount = 0;
        }
    } else {
        m_pacmanErrorCount = 0;
    }
}

void KhronicleDaemon::runJournalIngestion()
{
    if (m_journalBackoffCycles > 0) {
        --m_journalBackoffCycles;
        return;
    }

    const auto previousTimestamp = m_journalLastTimestamp;
    const JournalParseResult result = parseJournalSince(m_journalLastTimestamp);

    for (auto event : result.events) {
        RiskClassifier::classify(event);
        m_store->addEvent(event);
    }

    if (result.lastTimestamp > m_journalLastTimestamp) {
        m_journalLastTimestamp = result.lastTimestamp;
    }

#ifndef NDEBUG
    Q_ASSERT(result.lastTimestamp >= previousTimestamp);
#endif

    if (result.hadError) {
        if (m_journalErrorCount == 0) {
            qWarning() << "Khronicle: journal ingestion failed; journalctl may be unavailable.";
        }
        m_journalErrorCount++;
        if (m_journalErrorCount >= kMaxConsecutiveErrors) {
            qWarning() << "Khronicle: journal ingestion failed repeatedly, backing off.";
            m_journalBackoffCycles = kBackoffCycles;
            m_journalErrorCount = 0;
        }
    } else {
        m_journalErrorCount = 0;
    }
}

void KhronicleDaemon::runSnapshotCheck()
{
    SystemSnapshot current = buildCurrentSnapshot();

    if (!m_lastSnapshot.has_value()) {
        m_store->addSnapshot(current);
        m_lastSnapshot = current;
        return;
    }

#ifndef NDEBUG
    Q_ASSERT(current.timestamp >= m_lastSnapshot->timestamp);
#endif

    if (m_lastSnapshot->kernelVersion == current.kernelVersion) {
        return;
    }

    m_store->addSnapshot(current);

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
    event.riskLevel = "info";
    event.riskReason.clear();

    RiskClassifier::classify(event);
    m_store->addEvent(event);
    m_lastSnapshot = current;
}

void KhronicleDaemon::loadStateFromMeta()
{
    if (const auto pacmanCursor = m_store->getMeta("pacman_last_cursor")) {
        try {
            const long long value = std::stoll(*pacmanCursor);
            if (value >= 0) {
                m_pacmanCursor = *pacmanCursor;
            } else {
                m_pacmanCursor.reset();
            }
        } catch (const std::exception &) {
            m_pacmanCursor.reset();
        }
    }

    if (const auto journalTimestamp =
            m_store->getMeta("journal_last_timestamp")) {
        auto parsed = fromIso8601Utc(*journalTimestamp);
        if (parsed == std::chrono::system_clock::time_point{}) {
            m_journalLastTimestamp =
                std::chrono::system_clock::now() - kMaxJournalLookback;
        } else {
            m_journalLastTimestamp = parsed;
        }
    }
}

void KhronicleDaemon::persistStateToMeta()
{
    try {
        if (m_pacmanCursor.has_value()) {
            m_store->setMeta("pacman_last_cursor", *m_pacmanCursor);
        }
        m_store->setMeta("journal_last_timestamp",
                         timePointToIso(m_journalLastTimestamp));
    } catch (const std::exception &ex) {
        qWarning() << "Khronicle: failed to persist meta state:" << ex.what();
    }
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
