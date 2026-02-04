#include <QtTest/QtTest>

#include <QStringList>

#include "daemon/journal_parser.hpp"

class JournalParserTests : public QObject
{
    Q_OBJECT
private slots:
    void testParseFirmwareLine();
    void testParseGpuLine();
    void testNoEntries();
};

void JournalParserTests::testParseFirmwareLine()
{
    const QStringList lines = {
        "2026-02-04T12:00:00+0000 host fwupd[123]: firmware update installed: Device X"
    };
    const auto since = std::chrono::system_clock::now() - std::chrono::hours(1);
    const auto result = khronicle::parseJournalOutputLines(lines, since);

    QCOMPARE(result.events.size(), static_cast<size_t>(1));
    const auto &event = result.events.front();
    QCOMPARE(event.category, khronicle::EventCategory::Firmware);
    QCOMPARE(event.source, khronicle::EventSource::Journal);
    QVERIFY(QString::fromStdString(event.summary).contains("Firmware"));
}

void JournalParserTests::testParseGpuLine()
{
    const QStringList lines = {
        "2026-02-04T12:05:00+0000 host kernel: amdgpu version 1.2.3"
    };
    const auto since = std::chrono::system_clock::now() - std::chrono::hours(1);
    const auto result = khronicle::parseJournalOutputLines(lines, since);

    QCOMPARE(result.events.size(), static_cast<size_t>(1));
    const auto &event = result.events.front();
    QCOMPARE(event.category, khronicle::EventCategory::GpuDriver);
    QVERIFY(event.afterState.contains("version"));
}

void JournalParserTests::testNoEntries()
{
    const QStringList lines;
    const auto since = std::chrono::system_clock::now() - std::chrono::hours(1);
    const auto result = khronicle::parseJournalOutputLines(lines, since);

    QCOMPARE(result.events.size(), static_cast<size_t>(0));
    QCOMPARE(result.lastTimestamp, since);
}

QTEST_MAIN(JournalParserTests)
#include "test_journal_parser.moc"
