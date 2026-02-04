#include <QtTest/QtTest>

#include <QTemporaryDir>

#include <filesystem>

#include <nlohmann/json.hpp>

#include "daemon/khronicle_store.hpp"
#include "daemon/watch_engine.hpp"

class RiskClassifierTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testRiskThresholds();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;

    void resetDb();
    std::filesystem::path dbPath() const;
};

void RiskClassifierTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void RiskClassifierTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

std::filesystem::path RiskClassifierTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void RiskClassifierTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

void RiskClassifierTests::testRiskThresholds()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::WatchEngine engine(store);

    khronicle::WatchRule ruleImportant;
    ruleImportant.id = "rule-important";
    ruleImportant.name = "Important";
    ruleImportant.scope = khronicle::WatchScope::Event;
    ruleImportant.severity = khronicle::WatchSeverity::Warning;
    ruleImportant.riskLevelAtLeast = "important";
    store.upsertWatchRule(ruleImportant);

    khronicle::WatchRule ruleCritical = ruleImportant;
    ruleCritical.id = "rule-critical";
    ruleCritical.riskLevelAtLeast = "critical";
    store.upsertWatchRule(ruleCritical);

    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "kernel";
    event.afterState = nlohmann::json{{"riskLevel", "important"}};
    event.hostId = store.getHostIdentity().hostId;

    engine.evaluateEvent(event);

    const auto watchSignals = store.getWatchSignalsSince(
        std::chrono::system_clock::time_point{});
    QCOMPARE(watchSignals.size(), static_cast<size_t>(1));
    QCOMPARE(QString::fromStdString(watchSignals.front().ruleId),
             QStringLiteral("rule-important"));
}

QTEST_MAIN(RiskClassifierTests)
#include "test_risk_classifier.moc"
