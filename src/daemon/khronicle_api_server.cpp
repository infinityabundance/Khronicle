#include "daemon/khronicle_api_server.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QUuid>

#include <unistd.h>

#include "common/json_utils.hpp"
#include "common/logging.hpp"
#include "debug/scenario_capture.hpp"
#include "daemon/counterfactual.hpp"

namespace khronicle {

namespace {

QString runtimeSocketPath()
{
    const QString socketName = qEnvironmentVariable("KHRONICLE_SOCKET_NAME");
    if (!socketName.isEmpty()) {
        return socketName;
    }

    QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtimeDir.isEmpty()) {
        runtimeDir =
            QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    }
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
    if (socketPath.contains('/')) {
        const QFileInfo socketInfo(socketPath);
        if (!QDir().mkpath(socketInfo.absolutePath())) {
            qWarning() << "Failed to create runtime socket directory"
                       << socketInfo.absolutePath();
            return false;
        }

        if (QFile::exists(socketPath)) {
            if (!QLocalServer::removeServer(socketPath)) {
                qWarning() << "Failed to remove existing Khronicle socket" << socketPath;
                return false;
            }
        }
    } else {
        QLocalServer::removeServer(socketPath);
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
    if (!socket) {
        return;
    }
    const QByteArray response = handleRequestPayload(payload);
    socket->write(response);
    socket->flush();
    socket->disconnectFromServer();
}

QByteArray KhronicleApiServer::handleRequestPayload(const QByteArray &payload)
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
        return makeErrorResponse("Invalid JSON payload");
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
        return makeErrorResponse("Missing method", id);
    }

    const std::string method = parsed["method"].get<std::string>();
    nlohmann::json params = nlohmann::json::object();
    if (parsed.contains("params")) {
        if (!parsed["params"].is_object()) {
            return makeErrorResponse("Invalid params", id);
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
              (nlohmann::json{{"method", method},
                             {"paramKeys", paramKeys}}));

    if (ScenarioCapture::isEnabled()) {
        ScenarioCapture::recordStep(nlohmann::json{
            {"action", "api_call"},
            {"context", {{"method", method}, {"params", params}}}
        });
    }

    auto start = std::chrono::steady_clock::now();
    try {
        if (method == "get_changes_since") {
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                return makeErrorResponse("Invalid since timestamp", id);
            }

            const auto events = m_store.getEventsSince(since);
            nlohmann::json result;
            result["events"] = events;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "get_changes_between") {
            const std::string fromValue = params.value("from", "");
            const std::string toValue = params.value("to", "");
            const auto from = fromIso8601Utc(fromValue);
            const auto to = fromIso8601Utc(toValue);
            if (from == std::chrono::system_clock::time_point{}
                || to == std::chrono::system_clock::time_point{}) {
                return makeErrorResponse("Invalid from/to timestamp", id);
            }

            const auto events = m_store.getEventsBetween(from, to);
            nlohmann::json result;
            result["events"] = events;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "list_snapshots") {
            const auto snapshots = m_store.listSnapshots();
            nlohmann::json result;
            result["snapshots"] = snapshots;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "get_snapshot") {
            const std::string snapshotId = params.value("id", "");
            if (snapshotId.empty()) {
                return makeErrorResponse("Missing snapshot id", id);
            }
            const auto snapshot = m_store.getSnapshot(snapshotId);
            if (!snapshot.has_value()) {
                return makeErrorResponse("Snapshot not found", id);
            }
            nlohmann::json result;
            result["snapshot"] = *snapshot;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "diff_snapshots") {
            const std::string aId = params.value("a", "");
            const std::string bId = params.value("b", "");
            if (aId.empty() || bId.empty()) {
                return makeErrorResponse("Missing snapshot ids", id);
            }
            const auto diff = m_store.diffSnapshots(aId, bId);
            nlohmann::json result;
            result["diff"] = diff;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "summary_since") {
            // INVARIANT: Summaries are interpretations derived from stored facts.
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                return makeErrorResponse("Invalid since timestamp", id);
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

            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "list_watch_rules") {
            const auto rules = m_store.listWatchRules();
            nlohmann::json result;
            result["rules"] = rules;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "upsert_watch_rule") {
            if (!params.contains("rule") || !params["rule"].is_object()) {
                return makeErrorResponse("Missing rule object", id);
            }
            WatchRule rule = params["rule"].get<WatchRule>();
            if (rule.id.empty()) {
                return makeErrorResponse("Missing rule id", id);
            }
            m_store.upsertWatchRule(rule);
            nlohmann::json result;
            result["ok"] = true;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "delete_watch_rule") {
            const std::string ruleId = params.value("id", "");
            if (ruleId.empty()) {
                return makeErrorResponse("Missing rule id", id);
            }
            m_store.deleteWatchRule(ruleId);
            nlohmann::json result;
            result["ok"] = true;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "get_watch_signals_since") {
            const std::string sinceValue = params.value("since", "");
            const auto since = fromIso8601Utc(sinceValue);
            if (since == std::chrono::system_clock::time_point{}) {
                return makeErrorResponse("Invalid since timestamp", id);
            }
            const auto watchSignals = m_store.getWatchSignalsSince(since);
            nlohmann::json result;
            result["signals"] = watchSignals;
            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "explain_change_between") {
            // INVARIANT: Explanations are interpretive, not causal assertions.
            const std::string fromValue = params.value("from", "");
            const std::string toValue = params.value("to", "");
            const auto from = fromIso8601Utc(fromValue);
            const auto to = fromIso8601Utc(toValue);
            if (from == std::chrono::system_clock::time_point{}
                || to == std::chrono::system_clock::time_point{}) {
                return makeErrorResponse("Invalid from/to timestamp", id);
            }

            const auto baseline = m_store.getSnapshotBefore(from);
            const auto comparison = m_store.getSnapshotAfter(to);
            if (!baseline.has_value() || !comparison.has_value()) {
                return makeErrorResponse("Snapshots not found", id);
            }

            const auto events = m_store.getEventsBetween(from, to);
            const auto resultData =
                computeCounterfactual(*baseline, *comparison, events);

            nlohmann::json result;
            result["baselineSnapshot"] = resultData.baselineSnapshotId;
            result["comparisonSnapshot"] = resultData.comparisonSnapshotId;
            result["summary"] = resultData.explanationSummary;
            result["diff"] = resultData.diff;

            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        if (method == "what_changed_since_last_good") {
            const std::string referenceId = params.value("referenceSnapshotId", "");
            if (referenceId.empty()) {
                return makeErrorResponse("Missing referenceSnapshotId", id);
            }

            const auto baseline = m_store.getSnapshot(referenceId);
            const auto latest = latestSnapshot(m_store.listSnapshots());
            if (!baseline.has_value() || !latest.has_value()) {
                return makeErrorResponse("Snapshots not found", id);
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

            KLOG_INFO(QStringLiteral("KhronicleApiServer"),
                      QStringLiteral("handleRequest"),
                      QStringLiteral("api_request_completed"),
                      QStringLiteral("client_call"),
                      QStringLiteral("json_rpc"),
                      khronicle::logging::defaultWho(),
                      corrId,
                      (nlohmann::json{{"method", method},
                                     {"durationMs",
                                      std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count()}}));
            return makeResultResponse(result, id);
        }

        KLOG_WARN(QStringLiteral("KhronicleApiServer"),
                  QStringLiteral("handleRequest"),
                  QStringLiteral("api_request_error"),
                  QStringLiteral("unknown_method"),
                  QStringLiteral("json_rpc"),
                  khronicle::logging::defaultWho(),
                  corrId,
                  (nlohmann::json{{"method", method}}));
        return makeErrorResponse("Unknown method", id);
    } catch (const std::exception &ex) {
        KLOG_ERROR(QStringLiteral("KhronicleApiServer"),
                   QStringLiteral("handleRequest"),
                   QStringLiteral("api_request_error"),
                   QStringLiteral("exception"),
                   QStringLiteral("json_rpc"),
                   khronicle::logging::defaultWho(),
                   corrId,
                   (nlohmann::json{{"what", ex.what()}}));
        return makeErrorResponse(ex.what(), id);
    }
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
