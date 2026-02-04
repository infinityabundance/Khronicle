#include <QtTest/QtTest>

#include "daemon/risk_classifier.hpp"
#include "common/models.hpp"

class RiskClassifierTests : public QObject
{
    Q_OBJECT
private slots:
    void testKernelCritical();
    void testGpuImportant();
    void testFirmwareImportant();
    void testDowngradeImportant();
    void testInfoDefault();
};

void RiskClassifierTests::testKernelCritical()
{
    khronicle::KhronicleEvent event;
    event.category = khronicle::EventCategory::Kernel;
    event.summary = "Kernel upgraded";

    khronicle::RiskClassifier::classify(event);

    QCOMPARE(QString::fromStdString(event.riskLevel), QStringLiteral("critical"));
    QVERIFY(!event.riskReason.empty());
}

void RiskClassifierTests::testGpuImportant()
{
    khronicle::KhronicleEvent event;
    event.category = khronicle::EventCategory::GpuDriver;
    event.summary = "GPU driver updated";

    khronicle::RiskClassifier::classify(event);

    QCOMPARE(QString::fromStdString(event.riskLevel), QStringLiteral("important"));
    QVERIFY(!event.riskReason.empty());
}

void RiskClassifierTests::testFirmwareImportant()
{
    khronicle::KhronicleEvent event;
    event.category = khronicle::EventCategory::Firmware;
    event.summary = "Firmware updated";

    khronicle::RiskClassifier::classify(event);

    QCOMPARE(QString::fromStdString(event.riskLevel), QStringLiteral("important"));
    QVERIFY(!event.riskReason.empty());
}

void RiskClassifierTests::testDowngradeImportant()
{
    khronicle::KhronicleEvent event;
    event.category = khronicle::EventCategory::Package;
    event.summary = "downgraded mesa 24.3.1 -> 24.2.0";

    khronicle::RiskClassifier::classify(event);

    QCOMPARE(QString::fromStdString(event.riskLevel), QStringLiteral("important"));
    QVERIFY(!event.riskReason.empty());
}

void RiskClassifierTests::testInfoDefault()
{
    khronicle::KhronicleEvent event;
    event.category = khronicle::EventCategory::Package;
    event.summary = "upgraded zlib";

    khronicle::RiskClassifier::classify(event);

    QCOMPARE(QString::fromStdString(event.riskLevel), QStringLiteral("info"));
    QVERIFY(event.riskReason.empty());
}

QTEST_MAIN(RiskClassifierTests)
#include "test_risk_classifier.moc"
