#include "report/ReportCli.hpp"

#include <algorithm>
#include <iostream>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QProcess>

#include "common/json_utils.hpp"
#include "common/models.hpp"
#include "common/logging.hpp"
#include "debug/scenario_capture.hpp"
#include "daemon/khronicle_store.hpp"
#include "daemon/counterfactual.hpp"

namespace khronicle {

namespace {

QString usageText()
{
    return QStringLiteral(
        "Usage:\n"
        "  khronicle-report timeline --from ISO --to ISO [--format markdown|json]\n"
        "  khronicle-report diff --snapshot-a ID --snapshot-b ID [--format markdown|json]\n"
        "  khronicle-report explain --from ISO --to ISO [--format markdown|json]\n"
        "  khronicle-report bundle --from ISO --to ISO --out PATH\n"
        "  khronicle-report aggregate --input PATH --format markdown|json --out PATH\n");
}

QString humanizePath(const std::string &path)
{
    if (path == "kernelVersion") {
        return QStringLiteral("Kernel");
    }
    const std::string keyPrefix = "keyPackages.";
    if (path.rfind(keyPrefix, 0) == 0) {
        return QStringLiteral("Package: ")
            + QString::fromStdString(path.substr(keyPrefix.size()));
    }
    const std::string fwPrefix = "firmwareVersions.";
    if (path.rfind(fwPrefix, 0) == 0) {
        return QStringLiteral("Firmware: ")
            + QString::fromStdString(path.substr(fwPrefix.size()));
    }
    return QString::fromStdString(path);
}

std::string formatLocalTime(std::chrono::system_clock::time_point timestamp)
{
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            timestamp.time_since_epoch())
            .count(),
        Qt::UTC);
    dt = dt.toLocalTime();
    return dt.toString("yyyy-MM-dd HH:mm").toStdString();
}

void renderTimelineMarkdown(const std::vector<KhronicleEvent> &events,
                            std::chrono::system_clock::time_point from,
                            std::chrono::system_clock::time_point to)
{
    std::cout << "# Khronicle Timeline Report\n\n";
    std::cout << "Period: " << toIso8601Utc(from) << " -> "
              << toIso8601Utc(to) << "\n";
    std::cout << "Total events: " << events.size() << "\n\n";
    std::cout << "## Events\n\n";

    if (events.empty()) {
        std::cout << "No events in this period.\n";
        return;
    }

    for (const auto &event : events) {
        std::cout << "- [" << formatLocalTime(event.timestamp) << "] ("
                  << toCategoryString(event.category) << ", "
                  << toSourceString(event.source) << ") "
                  << event.summary << "\n";
        if (!event.details.empty()) {
            std::cout << "  - details: " << event.details << "\n";
        }
    }
}

void renderTimelineJson(const std::vector<KhronicleEvent> &events,
                        std::chrono::system_clock::time_point from,
                        std::chrono::system_clock::time_point to)
{
    nlohmann::json payload;
    payload["from"] = toIso8601Utc(from);
    payload["to"] = toIso8601Utc(to);
    payload["totalEvents"] = events.size();
    payload["events"] = events;

    std::cout << payload.dump(2) << std::endl;
}

void renderDiffMarkdown(const KhronicleDiff &diff,
                        const SystemSnapshot *snapshotA,
                        const SystemSnapshot *snapshotB)
{
    std::cout << "# Khronicle Snapshot Diff Report\n\n";
    std::cout << "From snapshot: " << diff.snapshotAId << "\n";
    std::cout << "To snapshot:   " << diff.snapshotBId << "\n\n";

    if (snapshotA) {
        std::cout << "From timestamp: " << toIso8601Utc(snapshotA->timestamp)
                  << "\n";
        std::cout << "From kernel: " << snapshotA->kernelVersion << "\n";
    }
    if (snapshotB) {
        std::cout << "To timestamp:   " << toIso8601Utc(snapshotB->timestamp)
                  << "\n";
        std::cout << "To kernel:   " << snapshotB->kernelVersion << "\n";
    }

    std::cout << "\n## Changed Fields\n\n";

    if (diff.changedFields.empty()) {
        std::cout << "No differences between snapshots.\n";
        return;
    }

    for (const auto &field : diff.changedFields) {
        std::cout << "### " << humanizePath(field.path).toStdString() << "\n\n";
        if (field.before.is_string()) {
            std::cout << "- Before: " << field.before.get<std::string>() << "\n";
        } else {
            std::cout << "- Before: " << field.before.dump() << "\n";
        }
        if (field.after.is_string()) {
            std::cout << "- After:  " << field.after.get<std::string>() << "\n\n";
        } else {
            std::cout << "- After:  " << field.after.dump() << "\n\n";
        }
    }
}

void renderDiffJson(const KhronicleDiff &diff,
                    const SystemSnapshot *snapshotA,
                    const SystemSnapshot *snapshotB)
{
    nlohmann::json payload;
    if (snapshotA) {
        payload["snapshotA"] = *snapshotA;
    }
    if (snapshotB) {
        payload["snapshotB"] = *snapshotB;
    }
    payload["diff"] = diff;

    std::cout << payload.dump(2) << std::endl;
}

QString getArgValue(const QStringList &args, const QString &key)
{
    const int idx = args.indexOf(key);
    if (idx < 0 || idx + 1 >= args.size()) {
        return {};
    }
    return args.at(idx + 1);
}

QString getFormat(const QStringList &args)
{
    const QString value = getArgValue(args, QStringLiteral("--format"));
    if (value.isEmpty()) {
        return QStringLiteral("markdown");
    }
    return value.toLower();
}

bool writeJsonFile(const QString &path, const nlohmann::json &payload)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = QByteArray::fromStdString(payload.dump(2));
    if (file.write(data) != data.size()) {
        return false;
    }
    return true;
}

nlohmann::json readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nlohmann::json();
    }
    const QByteArray data = file.readAll();
    try {
        return nlohmann::json::parse(data.toStdString());
    } catch (const nlohmann::json::parse_error &) {
        return nlohmann::json();
    }
}

} // namespace

int ReportCli::run(int argc, char *argv[])
{
    // CLI entry: parse the subcommand and delegate to the report handler.
    QStringList args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.push_back(QString::fromLocal8Bit(argv[i]));
    }

    if (args.size() < 2) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    const QString command = args.at(1);
    KLOG_INFO(QStringLiteral("ReportCli"),
              QStringLiteral("run"),
              QStringLiteral("report_cli_command"),
              QStringLiteral("user_invocation"),
              QStringLiteral("cli"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"command", command.toStdString()}});
    if (ScenarioCapture::isEnabled()) {
        ScenarioCapture::recordStep(nlohmann::json{
            {"action", "report_cli"},
            {"context", {{"command", command.toStdString()}}}
        });
    }
    if (command == QStringLiteral("timeline")) {
        return runTimelineReport(args);
    }
    if (command == QStringLiteral("diff")) {
        return runDiffReport(args);
    }
    if (command == QStringLiteral("explain")) {
        return runExplainReport(args);
    }
    if (command == QStringLiteral("bundle")) {
        return runBundleReport(args);
    }
    if (command == QStringLiteral("aggregate")) {
        return runAggregateReport(args);
    }

    std::cerr << usageText().toStdString();
    return 1;
}

int ReportCli::runTimelineReport(const QStringList &args)
{
    // Timeline reports read events between two timestamps and render them.
    const QString fromValue = getArgValue(args, QStringLiteral("--from"));
    const QString toValue = getArgValue(args, QStringLiteral("--to"));

    if (fromValue.isEmpty() || toValue.isEmpty()) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    const auto from = parseIso8601(fromValue);
    const auto to = parseIso8601(toValue);
    if (!from.has_value() || !to.has_value()) {
        std::cerr << "Invalid ISO8601 timestamp." << std::endl;
        return 1;
    }

    const QString format = getFormat(args);
    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Invalid format. Use markdown or json." << std::endl;
        return 1;
    }

    KhronicleStore store;
    const auto events = store.getEventsBetween(*from, *to);

    KLOG_INFO(QStringLiteral("ReportCli"),
              QStringLiteral("runTimelineReport"),
              QStringLiteral("report_timeline"),
              QStringLiteral("user_invocation"),
              QStringLiteral("sqlite_query"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"events", events.size()},
                             {"format", format.toStdString()}});
    if (format == QStringLiteral("json")) {
        renderTimelineJson(events, *from, *to);
    } else {
        renderTimelineMarkdown(events, *from, *to);
    }

    return 0;
}

int ReportCli::runDiffReport(const QStringList &args)
{
    // Diff reports compare two snapshots by id and render the delta.
    const QString snapshotAId = getArgValue(args, QStringLiteral("--snapshot-a"));
    const QString snapshotBId = getArgValue(args, QStringLiteral("--snapshot-b"));

    if (snapshotAId.isEmpty() || snapshotBId.isEmpty()) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    const QString format = getFormat(args);
    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Invalid format. Use markdown or json." << std::endl;
        return 1;
    }

    KhronicleStore store;
    const auto snapshotA = store.getSnapshot(snapshotAId.toStdString());
    const auto snapshotB = store.getSnapshot(snapshotBId.toStdString());

    if (!snapshotA.has_value() || !snapshotB.has_value()) {
        std::cerr << "Snapshot not found." << std::endl;
        return 1;
    }

    const KhronicleDiff diff =
        store.diffSnapshots(snapshotAId.toStdString(), snapshotBId.toStdString());

    KLOG_INFO(QStringLiteral("ReportCli"),
              QStringLiteral("runDiffReport"),
              QStringLiteral("report_diff"),
              QStringLiteral("user_invocation"),
              QStringLiteral("sqlite_query"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"snapshotA", snapshotAId.toStdString()},
                             {"snapshotB", snapshotBId.toStdString()},
                             {"format", format.toStdString()}});
    if (format == QStringLiteral("json")) {
        renderDiffJson(diff, &*snapshotA, &*snapshotB);
    } else {
        renderDiffMarkdown(diff, &*snapshotA, &*snapshotB);
    }

    return 0;
}

int ReportCli::runExplainReport(const QStringList &args)
{
    // Explain reports use counterfactual logic to summarize change causes.
    const QString fromValue = getArgValue(args, QStringLiteral("--from"));
    const QString toValue = getArgValue(args, QStringLiteral("--to"));

    if (fromValue.isEmpty() || toValue.isEmpty()) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    const auto from = parseIso8601(fromValue);
    const auto to = parseIso8601(toValue);
    if (!from.has_value() || !to.has_value()) {
        std::cerr << "Invalid ISO8601 timestamp." << std::endl;
        return 1;
    }

    const QString format = getFormat(args);
    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Invalid format. Use markdown or json." << std::endl;
        return 1;
    }

    KhronicleStore store;
    const auto baseline = store.getSnapshotBefore(*from);
    const auto comparison = store.getSnapshotAfter(*to);

    if (!baseline.has_value() || !comparison.has_value()) {
        std::cerr << "Snapshots not found." << std::endl;
        return 1;
    }

    const auto events = store.getEventsBetween(*from, *to);
    const auto result = computeCounterfactual(*baseline, *comparison, events);

    KLOG_INFO(QStringLiteral("ReportCli"),
              QStringLiteral("runExplainReport"),
              QStringLiteral("report_explain"),
              QStringLiteral("user_invocation"),
              QStringLiteral("sqlite_query"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"events", events.size()},
                             {"format", format.toStdString()}});
    if (format == QStringLiteral("json")) {
        nlohmann::json payload;
        payload["baselineSnapshot"] = result.baselineSnapshotId;
        payload["comparisonSnapshot"] = result.comparisonSnapshotId;
        payload["summary"] = result.explanationSummary;
        payload["diff"] = result.diff;
        std::cout << payload.dump(2) << std::endl;
    } else {
        std::cout << "# Change Explanation\n\n";
        std::cout << "Between " << toIso8601Utc(*from) << " and "
                  << toIso8601Utc(*to) << ":\n\n";
        for (const auto &field : result.diff.changedFields) {
            std::cout << "- " << field.path << "\n";
        }
        std::cout << "\n" << result.explanationSummary << "\n";
    }

    return 0;
}

int ReportCli::runBundleReport(const QStringList &args)
{
    // Bundle reports export events/snapshots into a portable archive.
    const QString fromValue = getArgValue(args, QStringLiteral("--from"));
    const QString toValue = getArgValue(args, QStringLiteral("--to"));
    const QString outPath = getArgValue(args, QStringLiteral("--out"));

    if (fromValue.isEmpty() || toValue.isEmpty() || outPath.isEmpty()) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    const auto from = parseIso8601(fromValue);
    const auto to = parseIso8601(toValue);
    if (!from.has_value() || !to.has_value()) {
        std::cerr << "Invalid ISO8601 timestamp." << std::endl;
        return 1;
    }

    try {
        KhronicleStore store;
        const auto events = store.getEventsBetween(*from, *to);
        const auto snapshots = store.listSnapshots();

        std::vector<SystemSnapshot> filteredSnapshots;
        for (const auto &snapshot : snapshots) {
            if (snapshot.timestamp >= *from && snapshot.timestamp <= *to) {
                filteredSnapshots.push_back(snapshot);
            }
        }

        nlohmann::json metadata;
        metadata["hostIdentity"] = store.getHostIdentity();
        metadata["exportTimestamp"] =
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
        metadata["from"] = toIso8601Utc(*from);
        metadata["to"] = toIso8601Utc(*to);

        QTemporaryDir tempDir;
        if (!tempDir.isValid()) {
            std::cerr << "Failed to create temporary directory." << std::endl;
            return 1;
        }

        const QString bundleRoot = tempDir.path() + QDir::separator() + "bundle";
        if (!QDir().mkpath(bundleRoot)) {
            std::cerr << "Failed to create bundle directory." << std::endl;
            return 1;
        }

        if (!writeJsonFile(bundleRoot + QDir::separator() + "metadata.json",
                           metadata)
            || !writeJsonFile(bundleRoot + QDir::separator() + "events.json",
                              nlohmann::json(events))
            || !writeJsonFile(bundleRoot + QDir::separator() + "snapshots.json",
                              nlohmann::json(filteredSnapshots))
            || !writeJsonFile(bundleRoot + QDir::separator() + "diffs.json",
                              nlohmann::json::array())
            || !writeJsonFile(bundleRoot + QDir::separator() + "audit_log.json",
                              nlohmann::json::array())) {
            std::cerr << "Failed to write bundle files." << std::endl;
            return 1;
        }

        QFile::remove(outPath);
        QProcess tar;
        tar.start(QStringLiteral("tar"),
                  {QStringLiteral("-czf"),
                   outPath,
                   QStringLiteral("-C"),
                   tempDir.path(),
                   QStringLiteral("bundle")});

        if (!tar.waitForStarted() || !tar.waitForFinished()) {
            std::cerr << "Failed to create bundle archive." << std::endl;
            return 1;
        }
        if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
            std::cerr << "tar failed to create bundle." << std::endl;
            return 1;
        }
        KLOG_INFO(QStringLiteral("ReportCli"),
                  QStringLiteral("runBundleReport"),
                  QStringLiteral("report_bundle"),
                  QStringLiteral("user_invocation"),
                  QStringLiteral("bundle_export"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"events", events.size()},
                                 {"snapshots", filteredSnapshots.size()},
                                 {"out", outPath.toStdString()}});
    } catch (const std::exception &ex) {
        std::cerr << "Failed to open database: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}

int ReportCli::runAggregateReport(const QStringList &args)
{
    // Aggregate reports merge multiple bundles for fleet review.
    const QString inputPath = getArgValue(args, QStringLiteral("--input"));
    const QString outPath = getArgValue(args, QStringLiteral("--out"));
    const QString format = getFormat(args);

    if (inputPath.isEmpty() || outPath.isEmpty()) {
        std::cerr << usageText().toStdString();
        return 1;
    }

    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Invalid format. Use markdown or json." << std::endl;
        return 1;
    }

    QDir inputDir(inputPath);
    if (!inputDir.exists()) {
        std::cerr << "Input path does not exist." << std::endl;
        return 1;
    }

    nlohmann::json aggregate;
    aggregate["generatedAt"] =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    aggregate["hosts"] = nlohmann::json::array();

    const QFileInfoList entries = inputDir.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);

    for (const QFileInfo &entry : entries) {
        QString bundleDirPath;
        QTemporaryDir tempDir;

        if (entry.isDir()) {
            bundleDirPath = entry.absoluteFilePath();
            if (!QFile::exists(bundleDirPath + "/metadata.json")) {
                if (QFile::exists(bundleDirPath + "/bundle/metadata.json")) {
                    bundleDirPath = bundleDirPath + "/bundle";
                } else {
                    continue;
                }
            }
        } else if (entry.isFile() && entry.fileName().endsWith(".tar.gz")) {
            if (!tempDir.isValid()) {
                continue;
            }
            QProcess tar;
            tar.start(QStringLiteral("tar"),
                      {QStringLiteral("-xzf"),
                       entry.absoluteFilePath(),
                       QStringLiteral("-C"),
                       tempDir.path()});
            if (!tar.waitForFinished() || tar.exitCode() != 0) {
                continue;
            }
            if (QFile::exists(tempDir.path() + "/bundle/metadata.json")) {
                bundleDirPath = tempDir.path() + "/bundle";
            } else if (QFile::exists(tempDir.path() + "/metadata.json")) {
                bundleDirPath = tempDir.path();
            } else {
                continue;
            }
        } else {
            continue;
        }

        const nlohmann::json metadata =
            readJsonFile(bundleDirPath + "/metadata.json");
        if (metadata.is_null() || metadata.empty()) {
            continue;
        }

        nlohmann::json host;
        host["hostIdentity"] = metadata.value("hostIdentity", nlohmann::json::object());
        host["events"] = readJsonFile(bundleDirPath + "/events.json");
        host["snapshots"] = readJsonFile(bundleDirPath + "/snapshots.json");
        host["auditLog"] = readJsonFile(bundleDirPath + "/audit_log.json");
        aggregate["hosts"].push_back(host);
    }

    if (format == QStringLiteral("json")) {
        if (!writeJsonFile(outPath, aggregate)) {
            std::cerr << "Failed to write aggregate JSON." << std::endl;
            return 1;
        }
        KLOG_INFO(QStringLiteral("ReportCli"),
                  QStringLiteral("runAggregateReport"),
                  QStringLiteral("report_aggregate"),
                  QStringLiteral("user_invocation"),
                  QStringLiteral("bundle_aggregate"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"hosts", aggregate["hosts"].size()},
                                 {"out", outPath.toStdString()}});
        return 0;
    }

    QFile outFile(outPath);
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "Failed to write aggregate report." << std::endl;
        return 1;
    }

    QByteArray output;
    output += "# Khronicle Aggregate Report\n\n";
    output += "Aggregated from ";
    output += QByteArray::number(aggregate["hosts"].size());
    output += " hosts on ";
    output += aggregate["generatedAt"].get<std::string>().c_str();
    output += "\n\n## Hosts\n\n";

    for (const auto &host : aggregate["hosts"]) {
        const auto identity = host.value("hostIdentity", nlohmann::json::object());
        const std::string name = identity.value("displayName", "");
        const std::string hostname = identity.value("hostname", "");
        const std::string hostId = identity.value("hostId", "");
        output += "- ";
        if (!name.empty()) {
            output += name.c_str();
            if (!hostname.empty()) {
                output += " (";
                output += hostname.c_str();
                output += ")";
            }
        } else if (!hostname.empty()) {
            output += hostname.c_str();
        } else {
            output += "Host";
        }
        if (!hostId.empty()) {
            output += " [hostId: ";
            output += hostId.c_str();
            output += "]";
        }
        output += "\n";
    }

    output += "\n## Recent Changes (Last 24h)\n\n";

    const auto now = QDateTime::currentDateTimeUtc();
    const auto cutoff = now.addSecs(-24 * 3600).toString(Qt::ISODate).toStdString();

    for (const auto &host : aggregate["hosts"]) {
        const auto identity = host.value("hostIdentity", nlohmann::json::object());
        const std::string name = identity.value("displayName", "");
        const std::string hostname = identity.value("hostname", "");

        output += "### ";
        if (!name.empty()) {
            output += name.c_str();
        } else if (!hostname.empty()) {
            output += hostname.c_str();
        } else {
            output += "Host";
        }
        output += "\n";

        const auto events = host.value("events", nlohmann::json::array());
        for (const auto &event : events) {
            const std::string ts = event.value("timestamp", "");
            if (!ts.empty() && ts < cutoff) {
                continue;
            }
            const std::string summary = event.value("summary", "");
            const std::string category = event.value("category", "");
            const std::string risk = event.value("riskLevel", "info");
            output += "- [";
            output += ts.c_str();
            output += "] (";
            output += category.c_str();
            output += ", ";
            output += risk.c_str();
            output += ") ";
            output += summary.c_str();
            output += "\n";
        }
        output += "\n";
    }

    outFile.write(output);
    KLOG_INFO(QStringLiteral("ReportCli"),
              QStringLiteral("runAggregateReport"),
              QStringLiteral("report_aggregate"),
              QStringLiteral("user_invocation"),
              QStringLiteral("bundle_aggregate"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json{{"hosts", aggregate["hosts"].size()},
                             {"out", outPath.toStdString()}});
    return 0;
}

std::optional<std::chrono::system_clock::time_point> ReportCli::parseIso8601(
    const QString &value) const
{
    const auto parsed = fromIso8601Utc(value.toStdString());
    if (parsed == std::chrono::system_clock::time_point{}) {
        return std::nullopt;
    }
    return parsed;
}

} // namespace khronicle
