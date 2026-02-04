#pragma once

#include "common/models.hpp"

namespace khronicle {

class RiskClassifier
{
public:
    // Mutates event.riskLevel and event.riskReason in-place.
    static void classify(KhronicleEvent &event);
};

} // namespace khronicle
