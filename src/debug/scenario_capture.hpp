#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace khronicle {

class ScenarioCapture {
public:
    static bool isEnabled();
    static void start(const QString &scenarioId,
                      const QString &title,
                      const QString &description);
    static void recordStep(const nlohmann::json &step);
    static void finalize();

    static QString scenarioDir();
};

} // namespace khronicle
