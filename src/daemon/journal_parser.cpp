#include "daemon/journal_parser.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <string>

#include <QDateTime>
#include <QProcess>
#include <QStringList>

#include <nlohmann/json.hpp>

namespace khronicle {

namespace {

QString toIsoSince(std::chrono::system_clock::time_point since)
{
    // journalctl expects ISO-8601 timestamps in UTC.
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            since.time_since_epoch())
                            .count();
    QDateTime dt = QDateTime::fromMSecsSinceEpoch(millis, Qt::UTC);
    return dt.toString(Qt::ISODate);
}

std::optional<std::chrono::system_clock::time_point> parseTimestamp(
    const QString &value)
{
    // journalctl --output=short-iso prefixes each line with an ISO timestamp with offset.
    QDateTime dt = QDateTime::fromString(value, Qt::ISODate);
    if (!dt.isValid()) {
        return std::nullopt;
    }

    const qint64 millis = dt.toMSecsSinceEpoch();
    return std::chrono::system_clock::time_point{
        std::chrono::milliseconds{millis}};
}

QString extractMessage(const QString &line)
{
    // Format: <timestamp> <hostname> <unit>: <message>
    // Format: <timestamp> <hostname> <unit>: <message>
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) {
        return {};
    }
    QString remainder = line.mid(firstSpace + 1).trimmed();

    int secondSpace = remainder.indexOf(' ');
    if (secondSpace < 0) {
        return {};
    }
    QString afterHost = remainder.mid(secondSpace + 1).trimmed();

    int colon = afterHost.indexOf(": ");
    if (colon >= 0) {
        return afterHost.mid(colon + 2).trimmed();
    }
    return afterHost;
}

QString extractProcess(const QString &line)
{
    int firstSpace = line.indexOf(' ');
    if (firstSpace < 0) {
        return {};
    }
    QString remainder = line.mid(firstSpace + 1).trimmed();

    int secondSpace = remainder.indexOf(' ');
    if (secondSpace < 0) {
        return {};
    }
    QString afterHost = remainder.mid(secondSpace + 1).trimmed();

    int colon = afterHost.indexOf(": ");
    if (colon >= 0) {
        return afterHost.left(colon).trimmed();
    }
    return {};
}

QString extractVersionToken(const QString &message)
{
    QString lower = message.toLower();
    int index = lower.indexOf("version");
    if (index < 0) {
        return {};
    }

    int start = index + static_cast<int>(strlen("version"));
    while (start < message.size() && message[start].isSpace()) {
        ++start;
    }

    int end = start;
    while (end < message.size()) {
        const QChar ch = message[end];
        if (ch.isDigit() || ch == '.' || ch == '-' || ch == '_') {
            ++end;
        } else {
            break;
        }
    }

    return message.mid(start, end - start).trimmed();
}

bool messageHasGpuSignal(const QString &message)
{
    const QString lower = message.toLower();
    return lower.contains("firmware") || lower.contains("version")
        || lower.contains("loading nvidia driver");
}

} // namespace

JournalParseResult parseJournalSince(std::chrono::system_clock::time_point since)
{
    // Query journalctl incrementally, returning only events newer than "since".
    QProcess process;
    QString sinceArg = QStringLiteral("--since=%1").arg(toIsoSince(since));
    process.start(QStringLiteral("journalctl"),
                  {sinceArg, QStringLiteral("--output=short-iso")});

    if (!process.waitForStarted()) {
        // If journalctl cannot start, return empty events and keep lastTimestamp unchanged.
        JournalParseResult result;
        result.lastTimestamp = since;
        return result;
    }

    process.closeWriteChannel();

    if (!process.waitForFinished()) {
        // If journalctl fails, return empty events and keep lastTimestamp unchanged.
        JournalParseResult result;
        result.lastTimestamp = since;
        return result;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        JournalParseResult result;
        result.lastTimestamp = since;
        return result;
    }

    const QString output = QString::fromUtf8(process.readAllStandardOutput());
    const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    return parseJournalOutputLines(lines, since);
}

JournalParseResult parseJournalOutputLines(const QStringList &lines,
                                           std::chrono::system_clock::time_point since)
{
    JournalParseResult result;
    result.lastTimestamp = since;

    for (const QString &line : lines) {
        int space = line.indexOf(' ');
        if (space < 0) {
            continue;
        }
        const QString timestampText = line.left(space).trimmed();
        auto timestamp = parseTimestamp(timestampText);
        if (!timestamp.has_value()) {
            continue;
        }

        const QString processName = extractProcess(line).toLower();
        const QString message = extractMessage(line);
        const QString lowerMessage = message.toLower();

        // Match firmware updates (fwupd) and GPU driver/firmware messages.
        bool isFwupd = processName.contains("fwupd")
            || lowerMessage.contains("firmware update installed")
            || lowerMessage.contains("successfully installed firmware");

        bool isAmd = lowerMessage.contains("amdgpu");
        bool isNvidia = lowerMessage.contains("nvidia")
            || lowerMessage.contains("nvidia-modeset")
            || lowerMessage.contains("nvrm");

        if (!isFwupd && !(messageHasGpuSignal(message) && (isAmd || isNvidia))) {
            continue;
        }

        KhronicleEvent event;
        event.timestamp = *timestamp;
        event.source = EventSource::Journal;
        event.beforeState = nlohmann::json::object();
        event.afterState = nlohmann::json::object();
        event.details = line.toStdString();

        const QString isoTimestamp = QDateTime::fromMSecsSinceEpoch(
                                         std::chrono::duration_cast<std::chrono::milliseconds>(
                                             timestamp->time_since_epoch())
                                             .count(),
                                         Qt::UTC)
                                         .toString(Qt::ISODate);

        if (isFwupd) {
            event.category = EventCategory::Firmware;
            event.summary = "Firmware updated via fwupd";
            event.relatedPackages = {"fwupd"};
            event.id = "journal-" + isoTimestamp.toStdString() + "-fwupd-"
                + std::to_string(std::hash<std::string>{}(event.details));

            if (lowerMessage.contains("firmware update installed")) {
                event.summary = message.toStdString();
            }

            int colon = message.indexOf(':');
            if (colon >= 0 && colon + 1 < message.size()) {
                QString firmware = message.mid(colon + 1).trimmed();
                if (!firmware.isEmpty()) {
                    event.afterState["firmware"] = firmware.toStdString();
                }
            }
        } else {
            event.category = EventCategory::GpuDriver;
            event.relatedPackages = {isAmd ? "amdgpu" : "nvidia"};
            event.id = "journal-" + isoTimestamp.toStdString()
                + (isAmd ? "-amdgpu-" : "-nvidia-")
                + std::to_string(std::hash<std::string>{}(event.details));

            if (isAmd) {
                event.summary = "amdgpu firmware/version event";
            } else {
                event.summary = "NVIDIA driver version loaded";
            }

            const QString version = extractVersionToken(message);
            if (!version.isEmpty()) {
                event.afterState["version"] = version.toStdString();
                if (isAmd) {
                    event.summary = "amdgpu firmware version loaded";
                }
            }
        }

        result.events.push_back(std::move(event));

        // Track the highest timestamp so callers can persist a resume point.
        if (*timestamp > result.lastTimestamp) {
            result.lastTimestamp = *timestamp;
        }
    }

    return result;
}

} // namespace khronicle
