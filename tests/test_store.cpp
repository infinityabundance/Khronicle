#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>

#include "daemon/khronicle_store.hpp"
#include "common/models.hpp"

class StoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testMetaPersistence();
    void testDeduplication();

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

QTEST_MAIN(StoreTests)
#include "test_store.moc"
