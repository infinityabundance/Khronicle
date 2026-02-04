#include "daemon/change_explainer.hpp"

#include <sstream>

#include <nlohmann/json.hpp>

#include "common/logging.hpp"

namespace khronicle {

std::string explainChange(const KhronicleDiff &diff,
                          const std::vector<KhronicleEvent> &events)
{
    // INVARIANT: No silent inference.
    // Explanations are interpretive summaries derived from recorded facts.
    KLOG_DEBUG(QStringLiteral("ChangeExplainer"),
               QStringLiteral("explainChange"),
               QStringLiteral("explain_change_start"),
               QStringLiteral("interpretation_request"),
               QStringLiteral("summary"),
               khronicle::logging::defaultWho(),
               QString(),
               (nlohmann::json{{"eventCount", events.size()},
                              {"changedFields", diff.changedFields.size()}}));
    bool kernelChange = false;
    bool gpuChange = false;
    bool firmwareChange = false;
    int packageChanges = 0;

    for (const auto &field : diff.changedFields) {
        if (field.path == "kernelVersion") {
            kernelChange = true;
        } else if (field.path == "gpuDriver") {
            gpuChange = true;
        } else if (field.path == "firmwareVersions") {
            firmwareChange = true;
        } else if (field.path.rfind("keyPackages.", 0) == 0) {
            packageChanges++;
        }
    }

    bool sawKernelEvent = false;
    bool sawGpuEvent = false;
    bool sawFirmwareEvent = false;

    for (const auto &event : events) {
        switch (event.category) {
        case EventCategory::Kernel:
            sawKernelEvent = true;
            break;
        case EventCategory::GpuDriver:
            sawGpuEvent = true;
            break;
        case EventCategory::Firmware:
            sawFirmwareEvent = true;
            break;
        default:
            break;
        }
    }

    std::vector<std::string> highlights;
    if (kernelChange || sawKernelEvent) {
        highlights.push_back("kernel was upgraded");
    }
    if (gpuChange || sawGpuEvent) {
        highlights.push_back("GPU driver updated");
    }
    if (firmwareChange || sawFirmwareEvent) {
        highlights.push_back("firmware updated");
    }
    if (packageChanges > 0) {
        highlights.push_back("key packages changed");
    }

    if (highlights.empty()) {
        KLOG_INFO(QStringLiteral("ChangeExplainer"),
                  QStringLiteral("explainChange"),
                  QStringLiteral("explain_change_complete"),
                  QStringLiteral("interpretation_request"),
                  QStringLiteral("summary"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  (nlohmann::json{{"highlights", 0}}));
        return "No significant kernel, GPU, or firmware changes were detected during this interval.";
    }

    std::ostringstream summary;
    summary << "During this interval, the ";
    for (size_t i = 0; i < highlights.size(); ++i) {
        if (i > 0 && i + 1 == highlights.size()) {
            summary << " and ";
        } else if (i > 0) {
            summary << ", ";
        }
        summary << highlights[i];
    }
    summary << ". These changes may explain differences in system behavior.";
    KLOG_INFO(QStringLiteral("ChangeExplainer"),
              QStringLiteral("explainChange"),
              QStringLiteral("explain_change_complete"),
              QStringLiteral("interpretation_request"),
              QStringLiteral("summary"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"highlights", highlights.size()}}));
    return summary.str();
}

} // namespace khronicle
