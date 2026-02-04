#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace khronicle {

class ReplayHarness {
public:
    int runScenario(const QString &scenarioDir);

private:
    int runSteps(const nlohmann::json &steps);
    int runIngestionCycleStep(const nlohmann::json &step);
    int runApiStep(const nlohmann::json &step);
    int runReportStep(const nlohmann::json &step);

    int sendApiRequest(const QString &socketPath,
                       const QString &method,
                       const nlohmann::json &params);

    bool prepareScenarioDb(const QString &scenarioDir, const QString &replayHome);
};

} // namespace khronicle
