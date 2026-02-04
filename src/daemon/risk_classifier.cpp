#include "daemon/risk_classifier.hpp"

#include <cctype>

namespace khronicle {

namespace {

int severityRank(const std::string &level)
{
    if (level == "critical") {
        return 2;
    }
    if (level == "important") {
        return 1;
    }
    return 0;
}

void updateRisk(KhronicleEvent &event, const std::string &level,
                const std::string &reason)
{
    if (severityRank(level) > severityRank(event.riskLevel)) {
        event.riskLevel = level;
        event.riskReason.clear();
    }

    if (severityRank(level) == severityRank(event.riskLevel)) {
        if (!reason.empty()) {
            if (!event.riskReason.empty()) {
                event.riskReason += " ";
            }
            event.riskReason += reason;
            if (!event.riskReason.empty() && event.riskReason.back() != '.') {
                event.riskReason += ".";
            }
        }
    }
}

} // namespace

void RiskClassifier::classify(KhronicleEvent &event)
{
    event.riskLevel = "info";
    event.riskReason.clear();

    // Kernel changes are always critical.
    if (event.category == EventCategory::Kernel) {
        updateRisk(event, "critical", "Kernel version changed");
    }

    // GPU driver updates are important.
    if (event.category == EventCategory::GpuDriver) {
        updateRisk(event, "important", "GPU driver updated");
    }

    // Firmware updates are important.
    if (event.category == EventCategory::Firmware) {
        updateRisk(event, "important", "Firmware or microcode updated");
    }

    // Downgrades: detect based on summary text when available.
    std::string lowered = event.summary;
    for (auto &ch : lowered) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lowered.find("downgraded") != std::string::npos) {
        updateRisk(event, "important", "Package downgraded");
    }
}

} // namespace khronicle
