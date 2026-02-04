#pragma once

#include <string>
#include <vector>

#include "common/models.hpp"

namespace khronicle {

std::string explainChange(const KhronicleDiff &diff,
                          const std::vector<KhronicleEvent> &events);

} // namespace khronicle
