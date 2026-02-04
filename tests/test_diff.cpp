#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>
#include <set>

#include "daemon/khronicle_store.hpp"
#include "common/models.hpp"

class DiffTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSnapshotDiff();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void DiffTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void DiffTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path DiffTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void DiffTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void DiffTests::testSnapshotDiff()
{
    resetDb();

    khronicle::KhronicleStore store;

    khronicle::SystemSnapshot snapshotA;
    snapshotA.id = "snapshot-a";
    snapshotA.timestamp = std::chrono::system_clock::now();
    snapshotA.kernelVersion = "6.11.2";
    snapshotA.keyPackages = nlohmann::json::object({{"mesa", "24.2.0"}});

    khronicle::SystemSnapshot snapshotB = snapshotA;
    snapshotB.id = "snapshot-b";
    snapshotB.timestamp = snapshotA.timestamp + std::chrono::seconds(10);
    snapshotB.kernelVersion = "6.11.4";
    snapshotB.keyPackages = nlohmann::json::object({{"mesa", "24.3.1"}});

    store.addSnapshot(snapshotA);
    store.addSnapshot(snapshotB);

    const auto diff = store.diffSnapshots(snapshotA.id, snapshotB.id);

    QCOMPARE(static_cast<int>(diff.changedFields.size()), 2);

    std::set<std::string> paths;
    for (const auto &field : diff.changedFields) {
        paths.insert(field.path);
    }

    QVERIFY(paths.contains("kernelVersion"));
    QVERIFY(paths.contains("keyPackages.mesa"));
}

QTEST_MAIN(DiffTests)
#include "test_diff.moc"
