#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include "daemon/pacman_parser.hpp"

class PacmanParserTests : public QObject
{
    Q_OBJECT
private slots:
    void testSingleUpgrade();
    void testKernelUpgrade();
    void testDowngrade();
    void testCursorBehavior();
};

static QString writeLogFile(QTemporaryDir &tempDir, const QString &content)
{
    if (!tempDir.isValid()) {
        qWarning() << "Temp dir not valid";
        return QString();
    }
    const QString path = tempDir.path() + "/pacman.log";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "Failed to open file:" << path << file.errorString();
        return QString();
    }
    file.write(content.toUtf8());
    file.close();
    return path;
}

void PacmanParserTests::testSingleUpgrade()
{
    QTemporaryDir tempDir;
    const QString path = writeLogFile(
        tempDir,
        "[2026-02-04T12:00] [ALPM] upgraded mesa (1.0-1 -> 1.0-2)\n");

    const auto result = khronicle::parsePacmanLog(path.toStdString(), std::nullopt);
    QCOMPARE(result.events.size(), static_cast<size_t>(1));
    const auto &event = result.events.front();
    QCOMPARE(event.category, khronicle::EventCategory::GpuDriver);
    QCOMPARE(event.source, khronicle::EventSource::Pacman);
    QCOMPARE(QString::fromStdString(event.relatedPackages.front()), QStringLiteral("mesa"));
    QCOMPARE(QString::fromStdString(event.afterState.value("version", "")),
             QStringLiteral("1.0-2"));
}

void PacmanParserTests::testKernelUpgrade()
{
    QTemporaryDir tempDir;
    const QString path = writeLogFile(
        tempDir,
        "[2026-02-04T12:00] [ALPM] upgraded linux-cachyos (6.1-1 -> 6.1-2)\n");

    const auto result = khronicle::parsePacmanLog(path.toStdString(), std::nullopt);
    QCOMPARE(result.events.size(), static_cast<size_t>(1));
    QCOMPARE(result.events.front().category, khronicle::EventCategory::Kernel);
}

void PacmanParserTests::testDowngrade()
{
    QTemporaryDir tempDir;
    const QString path = writeLogFile(
        tempDir,
        "[2026-02-04T12:00] [ALPM] downgraded nvidia (550.1-1 -> 540.1-1)\n");

    const auto result = khronicle::parsePacmanLog(path.toStdString(), std::nullopt);
    QCOMPARE(result.events.size(), static_cast<size_t>(1));
    const auto &event = result.events.front();
    QVERIFY(QString::fromStdString(event.summary).contains("downgraded"));
    QCOMPARE(QString::fromStdString(event.beforeState.value("version", "")),
             QStringLiteral("550.1-1"));
}

void PacmanParserTests::testCursorBehavior()
{
    QTemporaryDir tempDir;
    const QString content =
        "[2026-02-04T12:00] [ALPM] upgraded mesa (1.0-1 -> 1.0-2)\n"
        "[2026-02-04T12:01] [ALPM] upgraded linux-cachyos (6.1-1 -> 6.1-2)\n";
    const QString path = writeLogFile(tempDir, content);

    const auto first = khronicle::parsePacmanLog(path.toStdString(), std::nullopt);
    QVERIFY(!first.newCursor.empty());

    const auto second = khronicle::parsePacmanLog(path.toStdString(), first.newCursor);
    QCOMPARE(second.events.size(), static_cast<size_t>(0));

    const auto oversized = khronicle::parsePacmanLog(path.toStdString(), std::string("999999"));
    QVERIFY(!oversized.newCursor.empty());
}

QTEST_MAIN(PacmanParserTests)
#include "test_pacman_parser.moc"
