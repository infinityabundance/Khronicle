#pragma once

#include <string>
#include <vector>

#include "common/models.hpp"

namespace khronicle {

struct CounterfactualResult {
    std::string baselineSnapshotId;
    std::string comparisonSnapshotId;
    KhronicleDiff diff;
    std::string explanationSummary;
};

CounterfactualResult computeCounterfactual(
    const SystemSnapshot &baseline,
    const SystemSnapshot &comparison,
    const std::vector<KhronicleEvent> &interveningEvents);

} // namespace khronicle
