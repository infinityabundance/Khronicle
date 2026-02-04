#include <QtTest/QtTest>

#include <nlohmann/json.hpp>

#include "daemon/counterfactual.hpp"

class TemporalReasoningTests : public QObject
{
    Q_OBJECT
private slots:
    void testCounterfactualDiffAndSummary();
};

void TemporalReasoningTests::testCounterfactualDiffAndSummary()
{
    khronicle::SystemSnapshot baseline;
    baseline.id = "baseline";
    baseline.kernelVersion = "6.1";
    baseline.gpuDriver = nlohmann::json{{"version", "1"}};
    baseline.firmwareVersions = nlohmann::json{{"fw", "1"}};
    baseline.keyPackages = nlohmann::json{{"linux", "6.1"}};

    khronicle::SystemSnapshot comparison;
    comparison.id = "comparison";
    comparison.kernelVersion = "6.2";
    comparison.gpuDriver = nlohmann::json{{"version", "2"}};
    comparison.firmwareVersions = nlohmann::json{{"fw", "1"}};
    comparison.keyPackages = nlohmann::json{{"linux", "6.2"}};

    khronicle::KhronicleEvent eventKernel;
    eventKernel.category = khronicle::EventCategory::Kernel;
    khronicle::KhronicleEvent eventGpu;
    eventGpu.category = khronicle::EventCategory::GpuDriver;

    const std::vector<khronicle::KhronicleEvent> events = {eventKernel, eventGpu};

    const auto result = khronicle::computeCounterfactual(baseline, comparison, events);

    bool sawKernel = false;
    bool sawGpu = false;
    for (const auto &field : result.diff.changedFields) {
        if (field.path == "kernelVersion") {
            sawKernel = true;
        }
        if (field.path == "gpuDriver") {
            sawGpu = true;
        }
    }

    QVERIFY(sawKernel);
    QVERIFY(sawGpu);
    QVERIFY(QString::fromStdString(result.explanationSummary).contains("kernel"));
    QVERIFY(QString::fromStdString(result.explanationSummary).contains("GPU"));
    QVERIFY(QString::fromStdString(result.explanationSummary).contains("may explain"));
}

QTEST_MAIN(TemporalReasoningTests)
#include "test_temporal_reasoning.moc"
