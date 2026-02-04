#include "report/ReportCli.hpp"

#include <algorithm>
#include <iostream>

#include <QCoreApplication>
#include <QDateTime>

#include "common/khronicle_version.hpp"
#include "common/json_utils.hpp"
#include "common/models.hpp"
#include "daemon/khronicle_store.hpp"

namespace khronicle {

namespace {

QString usageText()
{
    return QStringLiteral(
        "Khronicle-Report " KHRONICLE_VERSION "\n"
        "Usage:\n"
        "  khronicle-report timeline --from ISO --to ISO [--format markdown|json]\n"
        "  khronicle-report diff --snapshot-a ID --snapshot-b ID [--format markdown|json]\n");
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

} // namespace

int ReportCli::run(int argc, char *argv[])
{
    QStringList args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        args.push_back(QString::fromLocal8Bit(argv[i]));
    }

    if (args.size() < 2) {
        std::cerr << "Khronicle-Report: missing command.\n";
        std::cerr << usageText().toStdString();
        return 1;
    }

    const QString command = args.at(1);
    if (command == QStringLiteral("timeline")) {
        return runTimelineReport(args);
    }
    if (command == QStringLiteral("diff")) {
        return runDiffReport(args);
    }

    std::cerr << "Khronicle-Report: unknown command.\n";
    std::cerr << usageText().toStdString();
    return 1;
}

int ReportCli::runTimelineReport(const QStringList &args)
{
    const QString fromValue = getArgValue(args, QStringLiteral("--from"));
    const QString toValue = getArgValue(args, QStringLiteral("--to"));

    if (fromValue.isEmpty() || toValue.isEmpty()) {
        std::cerr << "Khronicle-Report: missing --from/--to.\n";
        std::cerr << usageText().toStdString();
        return 1;
    }

    const auto from = parseIso8601(fromValue);
    const auto to = parseIso8601(toValue);
    if (!from.has_value() || !to.has_value()) {
        std::cerr << "Khronicle-Report: invalid ISO8601 timestamp.\n";
        return 1;
    }

    const QString format = getFormat(args);
    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Khronicle-Report: invalid format. Use markdown or json.\n";
        return 1;
    }

    try {
        KhronicleStore store;
        const auto events = store.getEventsBetween(*from, *to);

        if (format == QStringLiteral("json")) {
            renderTimelineJson(events, *from, *to);
        } else {
            renderTimelineMarkdown(events, *from, *to);
        }
    } catch (const std::exception &ex) {
        std::cerr << "Khronicle-Report: failed to open database: " << ex.what()
                  << "\n";
        return 1;
    }

    return 0;
}

int ReportCli::runDiffReport(const QStringList &args)
{
    const QString snapshotAId = getArgValue(args, QStringLiteral("--snapshot-a"));
    const QString snapshotBId = getArgValue(args, QStringLiteral("--snapshot-b"));

    if (snapshotAId.isEmpty() || snapshotBId.isEmpty()) {
        std::cerr << "Khronicle-Report: missing --snapshot-a/--snapshot-b.\n";
        std::cerr << usageText().toStdString();
        return 1;
    }

    const QString format = getFormat(args);
    if (format != QStringLiteral("markdown") && format != QStringLiteral("json")) {
        std::cerr << "Khronicle-Report: invalid format. Use markdown or json.\n";
        return 1;
    }

    try {
        KhronicleStore store;
        const auto snapshotA = store.getSnapshot(snapshotAId.toStdString());
        const auto snapshotB = store.getSnapshot(snapshotBId.toStdString());

        if (!snapshotA.has_value() || !snapshotB.has_value()) {
            std::cerr << "Khronicle-Report: snapshot not found.\n";
            return 1;
        }

        const KhronicleDiff diff =
            store.diffSnapshots(snapshotAId.toStdString(), snapshotBId.toStdString());

        if (format == QStringLiteral("json")) {
            renderDiffJson(diff, &*snapshotA, &*snapshotB);
        } else {
            renderDiffMarkdown(diff, &*snapshotA, &*snapshotB);
        }
    } catch (const std::exception &ex) {
        std::cerr << "Khronicle-Report: failed to open database: " << ex.what()
                  << "\n";
        return 1;
    }

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
