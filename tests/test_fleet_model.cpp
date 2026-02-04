#include <QtTest/QtTest>

#include <QTemporaryDir>
#include <QFile>

#include "ui/backend/FleetModel.hpp"

class FleetModelTests : public QObject
{
    Q_OBJECT
private slots:
    void testLoadAggregate();
};

void FleetModelTests::testLoadAggregate()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path = tempDir.path() + "/aggregate.json";
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(R"({"hosts":[{"hostIdentity":{"hostId":"host-a","hostname":"alpha"},"events":[],"snapshots":[]} ]})");
    file.close();

    khronicle::FleetModel model;
    model.loadAggregateFile(path);

    const QVariantList hosts = model.hosts();
    QCOMPARE(hosts.size(), 1);
    const QVariantMap host = hosts.first().toMap();
    QCOMPARE(host.value("hostId").toString(), QStringLiteral("host-a"));
}

QTEST_MAIN(FleetModelTests)
#include "test_fleet_model.moc"
