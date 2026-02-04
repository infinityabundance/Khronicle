#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>
#include <QDir>

#include "report/ReportCli.hpp"

static bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(data) == data.size();
}

class AggregateTests : public QObject
{
    Q_OBJECT
private slots:
    void testAggregate();
};

void AggregateTests::testAggregate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString bundleA = tempDir.path() + "/bundle-a";
    const QString bundleB = tempDir.path() + "/bundle-b";
    QVERIFY(QDir().mkpath(bundleA));
    QVERIFY(QDir().mkpath(bundleB));

    const QByteArray metadataA = R"({"hostIdentity":{"hostId":"host-a","hostname":"alpha"}})";
    const QByteArray metadataB = R"({"hostIdentity":{"hostId":"host-b","hostname":"beta"}})";
    const QByteArray events = "[]";
    const QByteArray snapshots = "[]";

    QVERIFY(writeFile(bundleA + "/metadata.json", metadataA));
    QVERIFY(writeFile(bundleA + "/events.json", events));
    QVERIFY(writeFile(bundleA + "/snapshots.json", snapshots));

    QVERIFY(writeFile(bundleB + "/metadata.json", metadataB));
    QVERIFY(writeFile(bundleB + "/events.json", events));
    QVERIFY(writeFile(bundleB + "/snapshots.json", snapshots));

    const QString outputPath = tempDir.path() + "/aggregate.json";

    const QByteArray inputArg = tempDir.path().toUtf8();
    const QByteArray outputArg = outputPath.toUtf8();

    khronicle::ReportCli cli;
    const char *argv[] = {
        "khronicle-report",
        "aggregate",
        "--input",
        inputArg.constData(),
        "--format",
        "json",
        "--out",
        outputArg.constData()
    };

    const int result = cli.run(9, const_cast<char **>(argv));
    QCOMPARE(result, 0);

    QFile outFile(outputPath);
    QVERIFY(outFile.open(QIODevice::ReadOnly));
    const QByteArray output = outFile.readAll();
    QVERIFY(output.contains("host-a"));
    QVERIFY(output.contains("host-b"));
}

QTEST_MAIN(AggregateTests)
#include "test_aggregate.moc"
