#include <QtTest/QtTest>

#include "daemon/change_explainer.hpp"
#include "common/models.hpp"

class ExplainerTests : public QObject
{
    Q_OBJECT
private slots:
    void testKernelGpuExplanation();
};

void ExplainerTests::testKernelGpuExplanation()
{
    khronicle::KhronicleDiff diff;
    diff.changedFields.push_back({"kernelVersion", "6.11.2", "6.11.4"});
    diff.changedFields.push_back({"gpuDriver", "old", "new"});

    khronicle::KhronicleEvent kernelEvent;
    kernelEvent.category = khronicle::EventCategory::Kernel;
    khronicle::KhronicleEvent gpuEvent;
    gpuEvent.category = khronicle::EventCategory::GpuDriver;

    std::vector<khronicle::KhronicleEvent> events = {kernelEvent, gpuEvent};

    const std::string summary = khronicle::explainChange(diff, events);
    const QString qSummary = QString::fromStdString(summary);

    QVERIFY(qSummary.contains("kernel"));
    QVERIFY(qSummary.contains("GPU"));
}

QTEST_MAIN(ExplainerTests)
#include "test_explainer.moc"
