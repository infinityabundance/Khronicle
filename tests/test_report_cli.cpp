#include <QtTest/QtTest>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include <filesystem>
#include <sstream>
#include <iostream>

#include <nlohmann/json.hpp>

#include "report/ReportCli.hpp"
#include "daemon/khronicle_store.hpp"
#include "common/json_utils.hpp"

class ReportCliTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testTimelineJson();
    void testDiffJson();
    void testBundleAndAggregate();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
    int runCli(const QStringList &args, std::string &out);
};

void ReportCliTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void ReportCliTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path ReportCliTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void ReportCliTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

int ReportCliTests::runCli(const QStringList &args, std::string &out)
{
    std::stringstream buffer;
    auto *oldBuf = std::cout.rdbuf(buffer.rdbuf());
    auto *oldErr = std::cerr.rdbuf(buffer.rdbuf());

    khronicle::ReportCli cli;
    std::vector<QByteArray> utf8Args;
    std::vector<char *> rawArgs;
    for (const QString &arg : args) {
        utf8Args.push_back(arg.toLocal8Bit());
    }
    for (auto &arg : utf8Args) {
        rawArgs.push_back(arg.data());
    }

    const int result = cli.run(rawArgs.size(), rawArgs.data());

    std::cout.rdbuf(oldBuf);
    std::cerr.rdbuf(oldErr);
    out = buffer.str();
    return result;
}

void ReportCliTests::testTimelineJson()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "kernel";
    event.hostId = store.getHostIdentity().hostId;
    store.addEvent(event);

    const auto from = khronicle::toIso8601Utc(event.timestamp - std::chrono::hours(1));
    const auto to = khronicle::toIso8601Utc(event.timestamp + std::chrono::hours(1));

    std::string output;
    const int code = runCli({"khronicle-report", "timeline",
                             "--from", QString::fromStdString(from),
                             "--to", QString::fromStdString(to),
                             "--format", "json"}, output);
    QCOMPARE(code, 0);

    const auto parsed = nlohmann::json::parse(output);
    QVERIFY(parsed.is_object());
    QVERIFY(parsed.contains("events"));
}

void ReportCliTests::testDiffJson()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::SystemSnapshot snapA;
    snapA.id = "snap-a";
    snapA.timestamp = std::chrono::system_clock::now() - std::chrono::hours(1);
    snapA.kernelVersion = "6.1";
    snapA.hostIdentity = store.getHostIdentity();

    khronicle::SystemSnapshot snapB = snapA;
    snapB.id = "snap-b";
    snapB.timestamp = std::chrono::system_clock::now();
    snapB.kernelVersion = "6.2";

    store.addSnapshot(snapA);
    store.addSnapshot(snapB);

    std::string output;
    const int code = runCli({"khronicle-report", "diff",
                             "--snapshot-a", "snap-a",
                             "--snapshot-b", "snap-b",
                             "--format", "json"}, output);
    QCOMPARE(code, 0);

    const auto parsed = nlohmann::json::parse(output);
    QVERIFY(parsed.contains("diff"));
}

void ReportCliTests::testBundleAndAggregate()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "kernel";
    event.hostId = store.getHostIdentity().hostId;
    store.addEvent(event);

    const auto from = khronicle::toIso8601Utc(event.timestamp - std::chrono::hours(1));
    const auto to = khronicle::toIso8601Utc(event.timestamp + std::chrono::hours(1));

    const QString bundlePath = m_tempDir.path() + "/bundle.tar.gz";
    std::string output;
    const int bundleCode = runCli({"khronicle-report", "bundle",
                                   "--from", QString::fromStdString(from),
                                   "--to", QString::fromStdString(to),
                                   "--out", bundlePath}, output);
    QCOMPARE(bundleCode, 0);
    QVERIFY(QFile::exists(bundlePath));

    QProcess tar;
    tar.start(QStringLiteral("tar"),
              {QStringLiteral("-tzf"), bundlePath});
    QVERIFY(tar.waitForFinished(2000));
    const QString list = QString::fromUtf8(tar.readAllStandardOutput());
    QVERIFY(list.contains("metadata.json"));
    QVERIFY(list.contains("events.json"));
    QVERIFY(list.contains("snapshots.json"));

    const QString aggregateDir = m_tempDir.path() + "/bundles";
    QDir().mkpath(aggregateDir + "/bundle-a");
    QFile metadata(aggregateDir + "/bundle-a/metadata.json");
    QVERIFY(metadata.open(QIODevice::WriteOnly | QIODevice::Truncate));
    metadata.write(R"({"hostIdentity":{"hostId":"a"}})");
    metadata.close();
    QFile events(aggregateDir + "/bundle-a/events.json");
    QVERIFY(events.open(QIODevice::WriteOnly | QIODevice::Truncate));
    events.write("[]");
    events.close();
    QFile snapshots(aggregateDir + "/bundle-a/snapshots.json");
    QVERIFY(snapshots.open(QIODevice::WriteOnly | QIODevice::Truncate));
    snapshots.write("[]");
    snapshots.close();

    QDir().mkpath(aggregateDir + "/bundle-b");
    QFile metadataB(aggregateDir + "/bundle-b/metadata.json");
    QVERIFY(metadataB.open(QIODevice::WriteOnly | QIODevice::Truncate));
    metadataB.write(R"({"hostIdentity":{"hostId":"b"}})");
    metadataB.close();
    QFile eventsB(aggregateDir + "/bundle-b/events.json");
    QVERIFY(eventsB.open(QIODevice::WriteOnly | QIODevice::Truncate));
    eventsB.write("[]");
    eventsB.close();
    QFile snapshotsB(aggregateDir + "/bundle-b/snapshots.json");
    QVERIFY(snapshotsB.open(QIODevice::WriteOnly | QIODevice::Truncate));
    snapshotsB.write("[]");
    snapshotsB.close();

    const QString aggregatePath = m_tempDir.path() + "/aggregate.json";
    const int aggCode = runCli({"khronicle-report", "aggregate",
                                "--input", aggregateDir,
                                "--out", aggregatePath,
                                "--format", "json"}, output);
    QCOMPARE(aggCode, 0);
    QVERIFY(QFile::exists(aggregatePath));

    QFile aggFile(aggregatePath);
    QVERIFY(aggFile.open(QIODevice::ReadOnly));
    const auto aggregateJson = nlohmann::json::parse(aggFile.readAll().toStdString());
    QVERIFY(aggregateJson.contains("hosts"));
    QCOMPARE(aggregateJson["hosts"].size(), static_cast<size_t>(2));
}

QTEST_MAIN(ReportCliTests)
#include "test_report_cli.moc"
