#include <QtTest/QtTest>

#include <nlohmann/json.hpp>

#include "common/json_utils.hpp"

class ModelsJsonTests : public QObject
{
    Q_OBJECT
private slots:
    void testEventRoundTrip();
    void testSnapshotRoundTrip();
    void testDiffRoundTrip();
    void testHostIdentityRoundTrip();
    void testWatchRuleRoundTrip();
    void testWatchSignalRoundTrip();
    void testMissingFieldsDefaults();

private:
    static qint64 toSeconds(std::chrono::system_clock::time_point t)
    {
        return std::chrono::duration_cast<std::chrono::seconds>(
            t.time_since_epoch()).count();
    }
};

void ModelsJsonTests::testEventRoundTrip()
{
    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "kernel upgraded";
    event.details = "details";
    event.beforeState = nlohmann::json{{"version", "1.0"}};
    event.afterState = nlohmann::json{{"version", "1.1"}, {"riskLevel", "critical"}};
    event.relatedPackages = {"linux-cachyos"};
    event.hostId = "host-a";

    nlohmann::json j = event;
    const auto parsed = j.get<khronicle::KhronicleEvent>();

    QCOMPARE(QString::fromStdString(parsed.id), QStringLiteral("event-1"));
    QCOMPARE(QString::fromStdString(parsed.summary), QStringLiteral("kernel upgraded"));
    QCOMPARE(parsed.category, khronicle::EventCategory::Kernel);
    QCOMPARE(parsed.source, khronicle::EventSource::Pacman);
    QCOMPARE(QString::fromStdString(parsed.hostId), QStringLiteral("host-a"));
    QCOMPARE(QString::fromStdString(parsed.afterState.value("riskLevel", "")),
             QStringLiteral("critical"));
    QCOMPARE(parsed.relatedPackages.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(parsed.relatedPackages.front()),
             QStringLiteral("linux-cachyos"));
    QCOMPARE(toSeconds(parsed.timestamp), toSeconds(event.timestamp));
}

void ModelsJsonTests::testSnapshotRoundTrip()
{
    khronicle::SystemSnapshot snapshot;
    snapshot.id = "snap-1";
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.kernelVersion = "6.1.0";
    snapshot.gpuDriver = nlohmann::json{{"version", "550"}};
    snapshot.firmwareVersions = nlohmann::json{{"fw", "1.2"}};
    snapshot.keyPackages = nlohmann::json{{"linux", "6.1.0"}};
    snapshot.hostIdentity = khronicle::HostIdentity{"host-a", "alpha", "Alpha", "Linux", "x86"};

    nlohmann::json j = snapshot;
    const auto parsed = j.get<khronicle::SystemSnapshot>();

    QCOMPARE(QString::fromStdString(parsed.id), QStringLiteral("snap-1"));
    QCOMPARE(QString::fromStdString(parsed.kernelVersion), QStringLiteral("6.1.0"));
    QCOMPARE(QString::fromStdString(parsed.hostIdentity.hostId), QStringLiteral("host-a"));
    QCOMPARE(toSeconds(parsed.timestamp), toSeconds(snapshot.timestamp));
    QVERIFY(parsed.gpuDriver.is_object());
    QVERIFY(parsed.firmwareVersions.is_object());
}

void ModelsJsonTests::testDiffRoundTrip()
{
    khronicle::KhronicleDiff diff;
    diff.snapshotAId = "a";
    diff.snapshotBId = "b";
    diff.changedFields.push_back({"kernelVersion", "1.0", "1.1"});

    nlohmann::json j = diff;
    const auto parsed = j.get<khronicle::KhronicleDiff>();

    QCOMPARE(QString::fromStdString(parsed.snapshotAId), QStringLiteral("a"));
    QCOMPARE(parsed.changedFields.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(parsed.changedFields.front().path),
             QStringLiteral("kernelVersion"));
}

void ModelsJsonTests::testHostIdentityRoundTrip()
{
    khronicle::HostIdentity identity{"host-a", "alpha", "Alpha", "Linux", "x86"};
    nlohmann::json j = identity;
    const auto parsed = j.get<khronicle::HostIdentity>();
    QCOMPARE(QString::fromStdString(parsed.hostId), QStringLiteral("host-a"));
    QCOMPARE(QString::fromStdString(parsed.hostname), QStringLiteral("alpha"));
}

void ModelsJsonTests::testWatchRuleRoundTrip()
{
    khronicle::WatchRule rule;
    rule.id = "rule-1";
    rule.name = "Kernel guard";
    rule.description = "Kernel change";
    rule.scope = khronicle::WatchScope::Event;
    rule.severity = khronicle::WatchSeverity::Critical;
    rule.enabled = false;
    rule.categoryEquals = "kernel";
    rule.riskLevelAtLeast = "important";
    rule.packageNameContains = "linux";
    rule.activeFrom = "02:00";
    rule.activeTo = "04:00";
    rule.extra = nlohmann::json{{"note", "test"}};

    nlohmann::json j = rule;
    const auto parsed = j.get<khronicle::WatchRule>();

    QCOMPARE(QString::fromStdString(parsed.id), QStringLiteral("rule-1"));
    QCOMPARE(parsed.scope, khronicle::WatchScope::Event);
    QCOMPARE(parsed.severity, khronicle::WatchSeverity::Critical);
    QCOMPARE(parsed.enabled, false);
    QCOMPARE(QString::fromStdString(parsed.activeFrom), QStringLiteral("02:00"));
    QCOMPARE(QString::fromStdString(parsed.extra.value("note", "")), QStringLiteral("test"));
}

void ModelsJsonTests::testWatchSignalRoundTrip()
{
    khronicle::WatchSignal signal;
    signal.id = "sig-1";
    signal.timestamp = std::chrono::system_clock::now();
    signal.ruleId = "rule-1";
    signal.ruleName = "Kernel guard";
    signal.severity = khronicle::WatchSeverity::Warning;
    signal.originType = "event";
    signal.originId = "event-1";
    signal.message = "matched";

    nlohmann::json j = signal;
    const auto parsed = j.get<khronicle::WatchSignal>();

    QCOMPARE(QString::fromStdString(parsed.ruleId), QStringLiteral("rule-1"));
    QCOMPARE(parsed.severity, khronicle::WatchSeverity::Warning);
    QCOMPARE(QString::fromStdString(parsed.originType), QStringLiteral("event"));
    QCOMPARE(toSeconds(parsed.timestamp), toSeconds(signal.timestamp));
}

void ModelsJsonTests::testMissingFieldsDefaults()
{
    nlohmann::json eventJson = {
        {"id", "event-legacy"},
        {"timestamp", khronicle::toIso8601Utc(std::chrono::system_clock::now())}
    };
    auto event = eventJson.get<khronicle::KhronicleEvent>();
    QCOMPARE(event.category, khronicle::EventCategory::System);
    QCOMPARE(event.source, khronicle::EventSource::Other);

    nlohmann::json ruleJson = {
        {"id", "rule-legacy"},
        {"name", "Legacy"},
        {"scope", "event"},
        {"severity", "info"}
    };
    auto rule = ruleJson.get<khronicle::WatchRule>();
    QCOMPARE(rule.enabled, true);

    nlohmann::json signalJson = {
        {"id", "sig-legacy"},
        {"timestamp", khronicle::toIso8601Utc(std::chrono::system_clock::now())},
        {"ruleId", "rule-legacy"},
        {"ruleName", "Legacy"},
        {"originType", "event"},
        {"originId", "event-legacy"}
    };
    auto signal = signalJson.get<khronicle::WatchSignal>();
    QCOMPARE(signal.severity, khronicle::WatchSeverity::Info);
}

QTEST_MAIN(ModelsJsonTests)
#include "test_models_and_json.moc"
