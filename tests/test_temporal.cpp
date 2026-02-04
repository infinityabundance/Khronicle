#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>

#include "daemon/khronicle_store.hpp"
#include "common/models.hpp"

class TemporalTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testSnapshotBeforeAfter();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void TemporalTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void TemporalTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path TemporalTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void TemporalTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void TemporalTests::testSnapshotBeforeAfter()
{
    resetDb();

    khronicle::KhronicleStore store;

    const auto base = std::chrono::system_clock::now();

    khronicle::SystemSnapshot snapA;
    snapA.id = "snap-a";
    snapA.timestamp = base - std::chrono::hours(2);
    snapA.kernelVersion = "6.11.2";

    khronicle::SystemSnapshot snapB = snapA;
    snapB.id = "snap-b";
    snapB.timestamp = base - std::chrono::hours(1);
    snapB.kernelVersion = "6.11.3";

    khronicle::SystemSnapshot snapC = snapA;
    snapC.id = "snap-c";
    snapC.timestamp = base;
    snapC.kernelVersion = "6.11.4";

    store.addSnapshot(snapA);
    store.addSnapshot(snapB);
    store.addSnapshot(snapC);

    const auto before = store.getSnapshotBefore(base - std::chrono::minutes(30));
    QVERIFY(before.has_value());
    QCOMPARE(QString::fromStdString(before->id), QStringLiteral("snap-b"));

    const auto after = store.getSnapshotAfter(base - std::chrono::minutes(30));
    QVERIFY(after.has_value());
    QCOMPARE(QString::fromStdString(after->id), QStringLiteral("snap-c"));
}

QTEST_MAIN(TemporalTests)
#include "test_temporal.moc"
