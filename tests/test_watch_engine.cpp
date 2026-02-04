#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <ctime>
#include <filesystem>

#include <nlohmann/json.hpp>

#include "daemon/khronicle_store.hpp"
#include "daemon/watch_engine.hpp"

class WatchEngineTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testEventRuleMatch();
    void testActiveWindow();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void WatchEngineTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void WatchEngineTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path WatchEngineTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void WatchEngineTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void WatchEngineTests::testEventRuleMatch()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::WatchRule rule;
    rule.id = "kernel-critical";
    rule.name = "Kernel critical";
    rule.scope = khronicle::WatchScope::Event;
    rule.severity = khronicle::WatchSeverity::Critical;
    rule.enabled = true;
    rule.categoryEquals = "kernel";
    rule.riskLevelAtLeast = "critical";
    store.upsertWatchRule(rule);

    khronicle::WatchEngine engine(store);

    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Other;
    event.summary = "Kernel update";
    event.afterState = nlohmann::json::object();
    event.afterState["riskLevel"] = "critical";
    event.hostId = store.getHostIdentity().hostId;

    engine.evaluateEvent(event);

    const auto signals = store.getWatchSignalsSince(
        std::chrono::system_clock::time_point{});
    QCOMPARE(static_cast<int>(signals.size()), 1);
    QCOMPARE(QString::fromStdString(signals[0].ruleId), QString("kernel-critical"));
    QCOMPARE(QString::fromStdString(signals[0].originType), QString("event"));
}

void WatchEngineTests::testActiveWindow()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::WatchRule rule;
    rule.id = "windowed";
    rule.name = "Outside maintenance";
    rule.scope = khronicle::WatchScope::Event;
    rule.severity = khronicle::WatchSeverity::Warning;
    rule.enabled = true;
    rule.categoryEquals = "kernel";
    rule.activeFrom = "02:00";
    rule.activeTo = "04:00";
    store.upsertWatchRule(rule);

    khronicle::WatchEngine engine(store);

    std::time_t now = std::time(nullptr);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif

    localTime.tm_hour = 3;
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    std::time_t insideTime = std::mktime(&localTime);

    khronicle::KhronicleEvent insideEvent;
    insideEvent.id = "event-inside";
    insideEvent.timestamp = std::chrono::system_clock::from_time_t(insideTime);
    insideEvent.category = khronicle::EventCategory::Kernel;
    insideEvent.source = khronicle::EventSource::Other;
    insideEvent.summary = "Kernel update";
    insideEvent.hostId = store.getHostIdentity().hostId;

    engine.evaluateEvent(insideEvent);

    const auto signalsInside = store.getWatchSignalsSince(
        std::chrono::system_clock::time_point{});
    QCOMPARE(static_cast<int>(signalsInside.size()), 0);

    localTime.tm_hour = 5;
    localTime.tm_min = 0;
    localTime.tm_sec = 0;
    std::time_t outsideTime = std::mktime(&localTime);

    khronicle::KhronicleEvent outsideEvent;
    outsideEvent.id = "event-outside";
    outsideEvent.timestamp = std::chrono::system_clock::from_time_t(outsideTime);
    outsideEvent.category = khronicle::EventCategory::Kernel;
    outsideEvent.source = khronicle::EventSource::Other;
    outsideEvent.summary = "Kernel update";
    outsideEvent.hostId = store.getHostIdentity().hostId;

    engine.evaluateEvent(outsideEvent);

    const auto signalsOutside = store.getWatchSignalsSince(
        std::chrono::system_clock::time_point{});
    QCOMPARE(static_cast<int>(signalsOutside.size()), 1);
}

QTEST_MAIN(WatchEngineTests)
#include "test_watch_engine.moc"
