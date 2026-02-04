#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include <filesystem>

#include "replay/ReplayHarness.hpp"
#include "daemon/khronicle_store.hpp"
#include "common/json_utils.hpp"

class ReplayHarnessTests : public QObject
{
    Q_OBJECT
private slots:
    void testRunScenario();
};

void ReplayHarnessTests::testRunScenario()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString scenarioDir = tempDir.path() + "/scenario";
    QDir().mkpath(scenarioDir);

    // Build a minimal DB in a temp HOME, then copy to scenario dir.
    QTemporaryDir homeDir;
    QVERIFY(homeDir.isValid());
    const QByteArray prevHome = qgetenv("HOME");
    qputenv("HOME", homeDir.path().toUtf8());

    {
        khronicle::KhronicleStore store;
        khronicle::SystemSnapshot snapshot;
        snapshot.id = "snap-1";
        snapshot.timestamp = std::chrono::system_clock::now();
        snapshot.kernelVersion = "6.1";
        snapshot.hostIdentity = store.getHostIdentity();
        store.addSnapshot(snapshot);
    }

    const QString dbPath = homeDir.path() + "/.local/share/khronicle/khronicle.db";
    QVERIFY(QFile::exists(dbPath));
    QVERIFY(QFile::copy(dbPath, scenarioDir + "/db.sqlite"));

    const nlohmann::json scenario = {
        {"id", "test"},
        {"title", "Replay"},
        {"description", "Test scenario"},
        {"khronicleVersion", "0.1.0"},
        {"entryPoint", "api_request"},
        {"steps", nlohmann::json::array({
            nlohmann::json{{"action", "api_call"},
                           {"context", {
                               {"method", "list_snapshots"},
                               {"params", nlohmann::json::object()}
                           }}}
        })}
    };

    QFile scenarioFile(scenarioDir + "/scenario.json");
    QVERIFY(scenarioFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    scenarioFile.write(QString::fromStdString(scenario.dump(2)).toUtf8());
    scenarioFile.close();

    khronicle::ReplayHarness harness;
    const int result = harness.runScenario(scenarioDir);
    QCOMPARE(result, 0);

    if (prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", prevHome);
    }
}

QTEST_MAIN(ReplayHarnessTests)
#include "test_replay.moc"
