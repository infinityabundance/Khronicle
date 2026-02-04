#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>

#include <nlohmann/json.hpp>

#include "daemon/khronicle_store.hpp"

class StoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testEventInsertAndQuery();
    void testSnapshots();
    void testMetaState();
    void testWatchRulesAndSignals();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void StoreTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void StoreTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path StoreTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void StoreTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void StoreTests::testEventInsertAndQuery()
{
    resetDb();

    khronicle::KhronicleStore store;
    const auto now = std::chrono::system_clock::now();

    khronicle::KhronicleEvent eventA;
    eventA.id = "event-a";
    eventA.timestamp = now - std::chrono::minutes(10);
    eventA.category = khronicle::EventCategory::Kernel;
    eventA.source = khronicle::EventSource::Pacman;
    eventA.summary = "kernel";
    eventA.beforeState = nlohmann::json::object();
    eventA.afterState = nlohmann::json::object();

    khronicle::KhronicleEvent eventB = eventA;
    eventB.id = "event-b";
    eventB.timestamp = now;
    eventB.category = khronicle::EventCategory::Firmware;

    store.addEvent(eventA);
    store.addEvent(eventB);

    const auto events = store.getEventsBetween(now - std::chrono::minutes(20), now);
    QCOMPARE(events.size(), static_cast<size_t>(2));
    QVERIFY(events.front().timestamp <= events.back().timestamp);
    QCOMPARE(QString::fromStdString(events.back().id), QStringLiteral("event-b"));
}

void StoreTests::testSnapshots()
{
    resetDb();

    khronicle::KhronicleStore store;
    const auto t1 = std::chrono::system_clock::now() - std::chrono::hours(2);
    const auto t2 = std::chrono::system_clock::now() - std::chrono::hours(1);

    khronicle::SystemSnapshot snapA;
    snapA.id = "snap-a";
    snapA.timestamp = t1;
    snapA.kernelVersion = "6.1";
    snapA.keyPackages = nlohmann::json{{"linux", "6.1"}};

    khronicle::SystemSnapshot snapB;
    snapB.id = "snap-b";
    snapB.timestamp = t2;
    snapB.kernelVersion = "6.2";
    snapB.keyPackages = nlohmann::json{{"linux", "6.2"}};

    store.addSnapshot(snapA);
    store.addSnapshot(snapB);

    const auto snapshots = store.listSnapshots();
    QCOMPARE(snapshots.size(), static_cast<size_t>(2));

    const auto before = store.getSnapshotBefore(t2);
    QVERIFY(before.has_value());
    QCOMPARE(QString::fromStdString(before->id), QStringLiteral("snap-b"));

    const auto after = store.getSnapshotAfter(t1);
    QVERIFY(after.has_value());
    QCOMPARE(QString::fromStdString(after->id), QStringLiteral("snap-a"));
}

void StoreTests::testMetaState()
{
    resetDb();

    khronicle::KhronicleStore store;
    store.setMeta("pacman_last_cursor", "1234");
    store.setMeta("journal_last_timestamp", "invalid");

    const auto cursor = store.getMeta("pacman_last_cursor");
    QVERIFY(cursor.has_value());
    QCOMPARE(QString::fromStdString(*cursor), QStringLiteral("1234"));

    const auto journal = store.getMeta("journal_last_timestamp");
    QVERIFY(journal.has_value());
    QCOMPARE(QString::fromStdString(*journal), QStringLiteral("invalid"));
}

void StoreTests::testWatchRulesAndSignals()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::WatchRule rule;
    rule.id = "rule-1";
    rule.name = "Kernel";
    rule.scope = khronicle::WatchScope::Event;
    rule.severity = khronicle::WatchSeverity::Critical;
    rule.categoryEquals = "kernel";
    store.upsertWatchRule(rule);

    const auto rules = store.listWatchRules();
    QCOMPARE(rules.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(rules.front().id), QStringLiteral("rule-1"));

    khronicle::WatchSignal signal;
    signal.id = "sig-1";
    signal.timestamp = std::chrono::system_clock::now();
    signal.ruleId = rule.id;
    signal.ruleName = rule.name;
    signal.severity = khronicle::WatchSeverity::Critical;
    signal.originType = "event";
    signal.originId = "event-1";
    signal.message = "matched";
    store.addWatchSignal(signal);

    const auto watchSignals = store.getWatchSignalsSince(
        std::chrono::system_clock::time_point{});
    QCOMPARE(watchSignals.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(watchSignals.front().ruleId), QStringLiteral("rule-1"));
}

QTEST_MAIN(StoreTests)
#include "test_store.moc"
