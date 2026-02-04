#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>
#include <optional>

#include "daemon/khronicle_store.hpp"
#include "common/models.hpp"
#include "daemon/risk_classifier.hpp"

class StoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testMetaPersistence();
    void testDeduplication();
    void testProvenancePersistence();
    void testAuditLog();
    void testIngestionGrouping();

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

void StoreTests::testMetaPersistence()
{
    resetDb();

    {
        khronicle::KhronicleStore store;
        store.setMeta("test_key", "value");
    }

    {
        khronicle::KhronicleStore store;
        const auto value = store.getMeta("test_key");
        QVERIFY(value.has_value());
        QCOMPARE(QString::fromStdString(*value), QStringLiteral("value"));
    }
}

void StoreTests::testDeduplication()
{
    resetDb();

    khronicle::KhronicleStore store;

    khronicle::KhronicleEvent eventA;
    eventA.id = "event-a";
    eventA.timestamp = std::chrono::system_clock::now();
    eventA.category = khronicle::EventCategory::Kernel;
    eventA.source = khronicle::EventSource::Pacman;
    eventA.summary = "Kernel upgraded";

    khronicle::KhronicleEvent eventB = eventA;
    eventB.id = "event-b";

    store.addEvent(eventA);
    store.addEvent(eventB);

    const auto events = store.getEventsBetween(
        eventA.timestamp - std::chrono::seconds(1),
        eventA.timestamp + std::chrono::seconds(1));

    QCOMPARE(static_cast<int>(events.size()), 1);
}

void StoreTests::testProvenancePersistence()
{
    resetDb();

    khronicle::KhronicleStore store;

    khronicle::KhronicleEvent event;
    event.id = "prov-event";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Package;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "Package updated";
    event.provenance.sourceType = "pacman.log";
    event.provenance.sourceRef = "/var/log/pacman.log";
    event.provenance.parserVersion = "pacman_parser@1";
    event.provenance.ingestionId = "ingestion-1";

    store.addEvent(event);

    const auto events = store.getEventsBetween(
        event.timestamp - std::chrono::seconds(1),
        event.timestamp + std::chrono::seconds(1));

    QCOMPARE(static_cast<int>(events.size()), 1);
    QCOMPARE(QString::fromStdString(events[0].provenance.sourceType),
             QStringLiteral("pacman.log"));
    QCOMPARE(QString::fromStdString(events[0].provenance.ingestionId),
             QStringLiteral("ingestion-1"));
}

void StoreTests::testAuditLog()
{
    resetDb();

    khronicle::KhronicleStore store;

    khronicle::KhronicleEvent event;
    event.id = "audit-event";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Uname;
    event.summary = "Kernel upgraded";

    khronicle::RiskClassifier::classify(event);
    store.addEvent(event);
    store.addRiskAuditIfNeeded(event);

    const auto entries = store.getAuditLogSince(
        event.timestamp - std::chrono::seconds(1), std::nullopt);
    QCOMPARE(static_cast<int>(entries.size()), 1);
    QCOMPARE(QString::fromStdString(entries[0].auditType),
             QStringLiteral("risk_classification"));
}

void StoreTests::testIngestionGrouping()
{
    resetDb();

    khronicle::KhronicleStore store;

    const auto now = std::chrono::system_clock::now();
    khronicle::KhronicleEvent eventA;
    eventA.id = "ingest-a";
    eventA.timestamp = now;
    eventA.category = khronicle::EventCategory::Package;
    eventA.source = khronicle::EventSource::Pacman;
    eventA.summary = "Package A updated";
    eventA.provenance.ingestionId = "ingestion-xyz";

    khronicle::KhronicleEvent eventB = eventA;
    eventB.id = "ingest-b";
    eventB.summary = "Package B updated";

    store.addEvent(eventA);
    store.addEvent(eventB);

    const auto events = store.getEventsBetween(
        now - std::chrono::seconds(1), now + std::chrono::seconds(1));
    QCOMPARE(static_cast<int>(events.size()), 2);
    QCOMPARE(QString::fromStdString(events[0].provenance.ingestionId),
             QStringLiteral("ingestion-xyz"));
    QCOMPARE(QString::fromStdString(events[1].provenance.ingestionId),
             QStringLiteral("ingestion-xyz"));
}

QTEST_MAIN(StoreTests)
#include "test_store.moc"
