#pragma once

#include <chrono>
#include <optional>

#include <QString>
#include <QStringList>

namespace khronicle {

class ReportCli
{
public:
    // CLI dispatcher for reports, bundles, and aggregation.
    // returns exit code
    int run(int argc, char *argv[]);

private:
    // Each subcommand loads from SQLite and renders output in the chosen format.
    int runTimelineReport(const QStringList &args);
    int runDiffReport(const QStringList &args);
    int runExplainReport(const QStringList &args);
    int runBundleReport(const QStringList &args);
    int runAggregateReport(const QStringList &args);

    std::optional<std::chrono::system_clock::time_point> parseIso8601(
        const QString &value) const;
};

} // namespace khronicle
