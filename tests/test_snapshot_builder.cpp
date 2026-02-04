#include <QtTest/QtTest>

#include "daemon/snapshot_builder.hpp"

class SnapshotBuilderTests : public QObject
{
    Q_OBJECT
private slots:
    void testBuildCurrentSnapshot();
};

void SnapshotBuilderTests::testBuildCurrentSnapshot()
{
    // Build a snapshot of the current system
    const khronicle::SystemSnapshot snapshot = khronicle::buildCurrentSnapshot();

    // Verify basic fields are populated
    QVERIFY(!snapshot.id.empty());
    QVERIFY(snapshot.timestamp != std::chrono::system_clock::time_point{});
    QVERIFY(!snapshot.kernelVersion.empty());

    // Verify the snapshot ID has a reasonable format (uuid-like or timestamp-based)
    QVERIFY(snapshot.id.size() >= 10);
}

QTEST_MAIN(SnapshotBuilderTests)
#include "test_snapshot_builder.moc"
