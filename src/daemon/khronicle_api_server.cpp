#include "daemon/khronicle_api_server.hpp"

#include <algorithm>
#include <optional>
#include <vector>

#include <QFile>
#include <QStandardPaths>
#include <QDebug>

#include <unistd.h>

#include "common/json_utils.hpp"
#include "common/khronicle_version.hpp"

namespace khronicle {

namespace {

QString runtimeSocketPath()
{
    QString runtimeDir =
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (runtimeDir.isEmpty()) {
        runtimeDir = QStringLiteral("/run/user/%1").arg(getuid());
    }
    return runtimeDir + QStringLiteral("/khronicle.sock");
}

std::optional<std::string> extractKernelVersion(const nlohmann::json &state)
{
    if (!state.is_object()) {
        return std::nullopt;
    }
    auto it = state.find("kernelVersion");
    if (it == state.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<std::chrono::system_clock::time_point> latestSnapshotTimestamp(
    const std::vector<SystemSnapshot> &snapshots)
{
    if (snapshots.empty()) {
        return std::nullopt;
    }

    auto latest = std::max_element(
        snapshots.begin(), snapshots.end(),
        [](const SystemSnapshot &a, const SystemSnapshot &b) {
            return a.timestamp < b.timestamp;
        });

    if (latest == snapshots.end()) {
        return std::nullopt;
    }
    return latest->timestamp;
}

} // namespace

KhronicleApiServer::KhronicleApiServer(KhronicleStore &store, QObject *parent)
    : QObject(parent)
    , m_store(store)
{
}

KhronicleApiServer::~KhronicleApiServer() = default;

bool KhronicleApiServer::start()
{
    const QString socketPath = runtimeSocketPath();

    if (QFile::exists(socketPath)) {
        if (!QLocalServer::removeServer(socketPath)) {
            qWarning() << "Khronicle: failed to remove existing socket" << socketPath;
            return false;
        }
    }

    if (!m_server.listen(socketPath)) {
        qWarning() << "Khronicle: failed to listen on socket" << socketPath
                   << m_server.errorString();
        return false;
    }

    connect(&m_server, &QLocalServer::newConnection,
            this, &KhronicleApiServer::handleNewConnection);

    qInfo() << "Khronicle: API server listening on" << socketPath;
    return true;
}

void KhronicleApiServer::handleNewConnection()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *socket = m_server.nextPendingConnection();
        if (!socket) {
            continue;
        }
        connect(socket, &QLocalSocket::readyRead,
                this, &KhronicleApiServer::handleClientReadyRead);
        connect(socket, &QLocalSocket::disconnected,
                socket, &QObject::deleteLater);
    }
}

void KhronicleApiServer::handleClientReadyRead()
{
    auto *socket = qobject_cast<QLocalSocket *>(sender());
    if (!socket) {
        return;
    }

    const QByteArray payload = socket->readAll();
    if (payload.isEmpty()) {
        return;
    }

    handleRequest(socket, payload);
}

void KhronicleApiServer::handleRequest(QLocalSocket *socket,
                                       const QByteArray &payload)
{
    const auto parsed = nlohmann::json::parse(payload.toStdString(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        const QByteArray response = makeErrorResponse("Invalid JSON payload");
        socket->write(response);
        socket->flush();
        socket->disconnectFromServer();
        return;
    }

    int id = -1;
    if (parsed.contains("id") && parsed["id"].is_number_integer()) {
        id = parsed["id"].get<int>();
    }

    if (!parsed.contains("method") || !parsed["method"].is_string()) {
        const QByteArray response = makeErrorResponse("Missing method", id);
        socket->write(response);
        socket->flush();
        socket->disconnectFromServer();
        return;
    }

    const std::string method = parsed["method"].get<std::string>();
    nlohmann::json params = nlohmann::json::object();
    if (parsed.contains("params")) {
        if (!parsed["params"].is_object()) {
            const QByteArray response = makeErrorResponse("Invalid params", id);
            socket->write(response);
            socket->flush();
            socket->disconnectFromServer();
            return;
        }
        params = parsed["params"];
    }

    try {
        if (method == "get_changes_since") {
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                const QByteArray response = makeErrorResponse("Invalid since timestamp", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto events = m_store.getEventsSince(since);
            nlohmann::json result;
            result["events"] = events;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "get_changes_between") {
            const std::string fromValue = params.value("from", "");
            const std::string toValue = params.value("to", "");
            const auto from = fromIso8601Utc(fromValue);
            const auto to = fromIso8601Utc(toValue);
            if (from == std::chrono::system_clock::time_point{}
                || to == std::chrono::system_clock::time_point{}) {
                const QByteArray response = makeErrorResponse("Invalid from/to timestamp", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto events = m_store.getEventsBetween(from, to);
            nlohmann::json result;
            result["events"] = events;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "list_snapshots") {
            const auto snapshots = m_store.listSnapshots();
            nlohmann::json result;
            result["snapshots"] = snapshots;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "get_snapshot") {
            const std::string snapshotId = params.value("id", "");
            if (snapshotId.empty()) {
                const QByteArray response = makeErrorResponse("Missing snapshot id", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            const auto snapshot = m_store.getSnapshot(snapshotId);
            if (!snapshot.has_value()) {
                const QByteArray response = makeErrorResponse("Snapshot not found", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            nlohmann::json result;
            result["snapshot"] = *snapshot;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "diff_snapshots") {
            const std::string aId = params.value("a", "");
            const std::string bId = params.value("b", "");
            if (aId.empty() || bId.empty()) {
                const QByteArray response = makeErrorResponse("Missing snapshot ids", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            const auto diff = m_store.diffSnapshots(aId, bId);
            nlohmann::json result;
            result["diff"] = diff;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "summary_since") {
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                const QByteArray response = makeErrorResponse("Invalid since timestamp", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto events = m_store.getEventsSince(since);
            int gpuEvents = 0;
            int firmwareEvents = 0;
            bool kernelChanged = false;
            std::string kernelFrom;
            std::string kernelTo;

            for (const auto &event : events) {
                switch (event.category) {
                case EventCategory::Kernel: {
                    kernelChanged = true;
                    if (kernelFrom.empty()) {
                        if (auto value = extractKernelVersion(event.beforeState)) {
                            kernelFrom = *value;
                        }
                    }
                    if (auto value = extractKernelVersion(event.afterState)) {
                        kernelTo = *value;
                    }
                    break;
                }
                case EventCategory::GpuDriver:
                    gpuEvents++;
                    break;
                case EventCategory::Firmware:
                    firmwareEvents++;
                    break;
                default:
                    break;
                }
            }

            nlohmann::json result;
            result["kernelChanged"] = kernelChanged;
            result["kernelFrom"] = kernelFrom;
            result["kernelTo"] = kernelTo;
            result["gpuEvents"] = gpuEvents;
            result["firmwareEvents"] = firmwareEvents;
            result["totalEvents"] = static_cast<int>(events.size());

            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else if (method == "get_daemon_status") {
            nlohmann::json result;
            result["version"] = KHRONICLE_VERSION;

            if (const auto cursor = m_store.getMeta("pacman_last_cursor")) {
                result["pacmanLastCursor"] = *cursor;
            }
            if (const auto journal = m_store.getMeta("journal_last_timestamp")) {
                result["journalLastTimestamp"] = *journal;
            }

            const auto snapshots = m_store.listSnapshots();
            if (const auto ts = latestSnapshotTimestamp(snapshots)) {
                result["lastSnapshotTimestamp"] = toIso8601Utc(*ts);
            }

            result["ingestionIntervalSeconds"] = 300;

            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
        } else {
            const QByteArray response = makeErrorResponse("Unknown method", id);
            socket->write(response);
        }
    } catch (const std::exception &ex) {
        const QByteArray response = makeErrorResponse(ex.what(), id);
        socket->write(response);
    }

    socket->flush();
    socket->disconnectFromServer();
}

QByteArray KhronicleApiServer::makeErrorResponse(const QString &message, int id) const
{
    nlohmann::json response;
    response["error"] = message.toStdString();
    response["id"] = id;
    return QByteArray::fromStdString(response.dump());
}

QByteArray KhronicleApiServer::makeResultResponse(const nlohmann::json &result,
                                                  int id) const
{
    nlohmann::json response;
    response["result"] = result;
    response["id"] = id;
    return QByteArray::fromStdString(response.dump());
}

} // namespace khronicle
