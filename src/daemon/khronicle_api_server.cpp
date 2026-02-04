#include "daemon/khronicle_api_server.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include <QUuid>

#include <unistd.h>

#include "common/json_utils.hpp"
#include "common/logging.hpp"
#include "daemon/counterfactual.hpp"

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

std::optional<SystemSnapshot> latestSnapshot(
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
    return *latest;
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
            qWarning() << "Failed to remove existing Khronicle socket" << socketPath;
            return false;
        }
    }

    if (!m_server.listen(socketPath)) {
        qWarning() << "Failed to listen on Khronicle socket" << socketPath
                   << m_server.errorString();
        return false;
    }

    connect(&m_server, &QLocalServer::newConnection,
            this, &KhronicleApiServer::handleNewConnection);

    qInfo() << "Khronicle API server listening on" << socketPath;
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
    // JSON-RPC-style request handler. All requests are local-only via UNIX socket.
    const QString corrId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    khronicle::logging::CorrelationScope corrScope(corrId);
    const auto parsed = nlohmann::json::parse(payload.toStdString(), nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        KLOG_WARN(QStringLiteral("KhronicleApiServer"),
                  QStringLiteral("handleRequest"),
                  QStringLiteral("api_request_error"),
                  QStringLiteral("parse_payload"),
                  QStringLiteral("json_parse"),
                  khronicle::logging::defaultWho(),
                  corrId,
                  nlohmann::json::object());
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
        KLOG_WARN(QStringLiteral("KhronicleApiServer"),
                  QStringLiteral("handleRequest"),
                  QStringLiteral("api_request_error"),
                  QStringLiteral("missing_method"),
                  QStringLiteral("json_parse"),
                  khronicle::logging::defaultWho(),
                  corrId,
                  nlohmann::json::object());
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

    nlohmann::json paramKeys = nlohmann::json::array();
    if (params.is_object()) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            paramKeys.push_back(it.key());
        }
    }
    KLOG_INFO(QStringLiteral("KhronicleApiServer"),
              QStringLiteral("handleRequest"),
              QStringLiteral("api_request_received"),
              QStringLiteral("client_call"),
              QStringLiteral("json_rpc"),
              khronicle::logging::defaultWho(),
              corrId,
              nlohmann::json{{"method", method},
                             {"paramKeys", paramKeys}});

    auto start = std::chrono::steady_clock::now();
    try {
        // Each branch populates a JSON result object or returns an error response.
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
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
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
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "list_snapshots") {
            const auto snapshots = m_store.listSnapshots();
            nlohmann::json result;
            result["snapshots"] = snapshots;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
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
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
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
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "summary_since") {
            // INVARIANT: Summaries are interpretations derived from stored facts.
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
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "list_watch_rules") {
            const auto rules = m_store.listWatchRules();
            nlohmann::json result;
            result["rules"] = rules;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "upsert_watch_rule") {
            if (!params.contains("rule") || !params["rule"].is_object()) {
                const QByteArray response = makeErrorResponse("Missing rule object", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            WatchRule rule = params["rule"].get<WatchRule>();
            if (rule.id.empty()) {
                const QByteArray response = makeErrorResponse("Missing rule id", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            m_store.upsertWatchRule(rule);
            nlohmann::json result;
            result["ok"] = true;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "delete_watch_rule") {
            const std::string ruleId = params.value("id", "");
            if (ruleId.empty()) {
                const QByteArray response = makeErrorResponse("Missing rule id", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            m_store.deleteWatchRule(ruleId);
            nlohmann::json result;
            result["ok"] = true;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "get_watch_signals_since") {
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                const QByteArray response = makeErrorResponse("Invalid since timestamp", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }
            const auto signals = m_store.getWatchSignalsSince(since);
            nlohmann::json result;
            result["signals"] = signals;
            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "explain_change_between") {
            // INVARIANT: Explanations are interpretive, not causal assertions.
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

            const auto baseline = m_store.getSnapshotBefore(from);
            const auto comparison = m_store.getSnapshotAfter(to);
            if (!baseline.has_value() || !comparison.has_value()) {
                const QByteArray response = makeErrorResponse("Snapshots not found", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto events = m_store.getEventsBetween(from, to);
            const auto resultData =
                computeCounterfactual(*baseline, *comparison, events);

            nlohmann::json result;
            result["baselineSnapshot"] = resultData.baselineSnapshotId;
            result["comparisonSnapshot"] = resultData.comparisonSnapshotId;
            result["summary"] = resultData.explanationSummary;
            result["diff"] = resultData.diff;

            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else if (method == "what_changed_since_last_good") {
            const std::string referenceId = params.value("referenceSnapshotId", "");
            if (referenceId.empty()) {
                const QByteArray response = makeErrorResponse("Missing referenceSnapshotId", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto baseline = m_store.getSnapshot(referenceId);
            const auto latest = latestSnapshot(m_store.listSnapshots());
            if (!baseline.has_value() || !latest.has_value()) {
                const QByteArray response = makeErrorResponse("Snapshots not found", id);
                socket->write(response);
                socket->flush();
                socket->disconnectFromServer();
                return;
            }

            const auto events = m_store.getEventsBetween(baseline->timestamp,
                                                        latest->timestamp);
            const auto resultData =
                computeCounterfactual(*baseline, *latest, events);

            nlohmann::json result;
            result["baselineSnapshot"] = resultData.baselineSnapshotId;
            result["comparisonSnapshot"] = resultData.comparisonSnapshotId;
            result["summary"] = resultData.explanationSummary;
            result["diff"] = resultData.diff;

            const QByteArray response = makeResultResponse(result, id);
            socket->write(response);
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}});
        } else {
            KLOG_WARN(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_error"),
                      QStringLiteral("unknown_method"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      nlohmann::json{{"method", method}});
            const QByteArray response = makeErrorResponse("Unknown method", id);
            socket->write(response);
        }
    } catch (const std::exception &ex) {
        KLOG_ERROR(QStringLiteral("KhronicleApiServer"),
                   QStringLiteral("handleRequest"),
                   QStringLiteral("api_request_error"),
                   QStringLiteral("exception"),
                   QStringLiteral("json_rpc"),
                   khronicle::logging::defaultWho(),
                   corrId,
                   nlohmann::json{{"what", ex.what()}});
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
