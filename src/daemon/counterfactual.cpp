#include "daemon/counterfactual.hpp"

#include <set>

#include "daemon/change_explainer.hpp"

namespace khronicle {

CounterfactualResult computeCounterfactual(
    const SystemSnapshot &baseline,
    const SystemSnapshot &comparison,
    const std::vector<KhronicleEvent> &interveningEvents)
{
    // INVARIANT: Facts precede interpretation.
    // Counterfactual output is derived from stored snapshots/events.
    CounterfactualResult result;
    result.baselineSnapshotId = baseline.id;
    result.comparisonSnapshotId = comparison.id;

    KhronicleDiff diff;
    diff.snapshotAId = baseline.id;
    diff.snapshotBId = comparison.id;

    if (baseline.kernelVersion != comparison.kernelVersion) {
        diff.changedFields.push_back({
            "kernelVersion",
            baseline.kernelVersion,
            comparison.kernelVersion,
        });
    }

    if (baseline.gpuDriver != comparison.gpuDriver) {
        diff.changedFields.push_back({
            "gpuDriver",
            baseline.gpuDriver,
            comparison.gpuDriver,
        });
    }

    if (baseline.firmwareVersions != comparison.firmwareVersions) {
        diff.changedFields.push_back({
            "firmwareVersions",
            baseline.firmwareVersions,
            comparison.firmwareVersions,
        });
    }

    nlohmann::json keyPackagesA = baseline.keyPackages.is_object()
        ? baseline.keyPackages
        : nlohmann::json::object();
    nlohmann::json keyPackagesB = comparison.keyPackages.is_object()
        ? comparison.keyPackages
        : nlohmann::json::object();

    std::set<std::string> keys;
    for (const auto &item : keyPackagesA.items()) {
        keys.insert(item.key());
    }
    for (const auto &item : keyPackagesB.items()) {
        keys.insert(item.key());
    }

    for (const auto &key : keys) {
        nlohmann::json before = keyPackagesA.contains(key)
            ? keyPackagesA.at(key)
            : nlohmann::json();
        nlohmann::json after = keyPackagesB.contains(key)
            ? keyPackagesB.at(key)
            : nlohmann::json();
        if (before != after) {
            diff.changedFields.push_back({
                "keyPackages." + key,
                before,
                after,
            });
        }
    }

    result.diff = diff;
    result.explanationSummary = explainChange(diff, interveningEvents);
    return result;
}

} // namespace khronicle
