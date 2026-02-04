#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include <nlohmann/json.hpp>

#include "common/logging.hpp"

class LoggingTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void testLogEventWrites();
    void testCodexTraceWrites();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;
};

void LoggingTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    qputenv("HOME", m_tempDir.path().toUtf8());
}

void LoggingTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }
}

void LoggingTests::testLogEventWrites()
{
    khronicle::logging::initLogging(QStringLiteral("khronicle-test"), false);
    const QString logPath = m_tempDir.path() + "/.local/share/khronicle/logs/khronicle-test.log";

    khronicle::logging::logEvent(khronicle::logging::LogLevel::Info,
                                 QStringLiteral("khronicle-test"),
                                 QStringLiteral("Test"),
                                 QStringLiteral("testLogEventWrites"),
                                 QStringLiteral("test_log"),
                                 QStringLiteral("unit_test"),
                                 QStringLiteral("direct_call"),
                                 khronicle::logging::defaultWho(),
                                 QStringLiteral("corr-1"),
                                 nlohmann::json{{"key", "value"}});

    QFile file(logPath);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray line = file.readLine();
    QVERIFY(!line.trimmed().isEmpty());

    const auto parsed = nlohmann::json::parse(line.toStdString());
    QCOMPARE(QString::fromStdString(parsed.value("what", "")), QStringLiteral("test_log"));
    QCOMPARE(QString::fromStdString(parsed.value("corr", "")), QStringLiteral("corr-1"));
}

void LoggingTests::testCodexTraceWrites()
{
    khronicle::logging::initLogging(QStringLiteral("khronicle-test"), true);
    const QString codexPath = m_tempDir.path() + "/.local/share/khronicle/logs/khronicle-test-codex.log";

    khronicle::logging::logEvent(khronicle::logging::LogLevel::Debug,
                                 QStringLiteral("khronicle-test"),
                                 QStringLiteral("Test"),
                                 QStringLiteral("testCodexTraceWrites"),
                                 QStringLiteral("test_codex"),
                                 QStringLiteral("unit_test"),
                                 QStringLiteral("direct_call"),
                                 khronicle::logging::defaultWho(),
                                 QStringLiteral("corr-2"),
                                 nlohmann::json::object());

    QFile file(codexPath);
    QVERIFY(file.exists());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray line = file.readLine();
    QVERIFY(!line.trimmed().isEmpty());
}

QTEST_MAIN(LoggingTests)
#include "test_logging.moc"
