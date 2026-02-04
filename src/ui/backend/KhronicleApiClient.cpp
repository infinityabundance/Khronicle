#include "ui/backend/KhronicleApiClient.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <nlohmann/json.hpp>

#include <unistd.h>

#include "common/logging.hpp"

namespace khronicle {

namespace {

QString toIso8601Utc(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

} // namespace

KhronicleApiClient::KhronicleApiClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
    , m_nextRequestId(1)
{
    connect(m_socket, &QLocalSocket::connected,
            this, &KhronicleApiClient::onSocketConnected);
    connect(m_socket, &QLocalSocket::errorOccurred,
            this, &KhronicleApiClient::onSocketError);
    connect(m_socket, &QLocalSocket::readyRead,
            this, &KhronicleApiClient::onSocketReadyRead);
}

KhronicleApiClient::~KhronicleApiClient() = default;

void KhronicleApiClient::connectToDaemon()
{
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        if (!m_connected) {
            m_connected = true;
            emit connectedChanged(true);
        }
        return;
    }

    KLOG_INFO(QStringLiteral("KhronicleApiClient"),
              QStringLiteral("connectToDaemon"),
              QStringLiteral("connect_daemon"),
              QStringLiteral("ui_start"),
              QStringLiteral("local_socket"),
              logging::defaultWho(),
              QString(),
              nlohmann::json{{"socketPath", socketPath().toStdString()}});
    m_socket->connectToServer(socketPath());
}

void KhronicleApiClient::loadChangesSince(const QDateTime &since)
{
    QJsonObject params;
    params["since"] = toIso8601Utc(since);
    sendRequest(QStringLiteral("get_changes_since"), params);
}

void KhronicleApiClient::loadChangesBetween(const QDateTime &from,
                                            const QDateTime &to)
{
    QJsonObject params;
    params["from"] = toIso8601Utc(from);
    params["to"] = toIso8601Utc(to);
    sendRequest(QStringLiteral("get_changes_between"), params);
}

void KhronicleApiClient::loadSummarySince(const QDateTime &since)
{
    QJsonObject params;
    params["since"] = toIso8601Utc(since);
    sendRequest(QStringLiteral("summary_since"), params);
}

void KhronicleApiClient::loadSnapshots()
{
    sendRequest(QStringLiteral("list_snapshots"), QJsonObject());
}

void KhronicleApiClient::loadDiff(const QString &snapshotAId,
                                 const QString &snapshotBId)
{
    QJsonObject params;
    params["a"] = snapshotAId;
    params["b"] = snapshotBId;
    sendRequest(QStringLiteral("diff_snapshots"), params);
}

void KhronicleApiClient::loadExplanationBetween(const QDateTime &from,
                                                const QDateTime &to)
{
    QJsonObject params;
    params["from"] = toIso8601Utc(from);
    params["to"] = toIso8601Utc(to);
    sendRequest(QStringLiteral("explain_change_between"), params);
}

void KhronicleApiClient::onSocketConnected()
{
    m_connected = true;
    emit connectedChanged(true);
}

void KhronicleApiClient::onSocketError(QLocalSocket::LocalSocketError error)
{
    Q_UNUSED(error)
    m_connected = false;
    emit connectedChanged(false);
    emit errorOccurred(m_socket->errorString());
}

void KhronicleApiClient::onSocketReadyRead()
{
    const QByteArray payload = m_socket->readAll();
    const QList<QByteArray> lines = payload.split('\n');

    for (const QByteArray &line : lines) {
        if (line.trimmed().isEmpty()) {
            continue;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            emit errorOccurred(QStringLiteral("Invalid JSON response"));
            continue;
        }

        handleResponse(doc.object());
    }
}

void KhronicleApiClient::sendRequest(const QString &method,
                                    const QJsonObject &params)
{
    // Requests are newline-delimited JSON messages over a persistent local socket.
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        connectToDaemon();
        emit errorOccurred(QStringLiteral("Not connected to Khronicle daemon"));
        KLOG_WARN(QStringLiteral("KhronicleApiClient"),
                  QStringLiteral("sendRequest"),
                  QStringLiteral("request_failed"),
                  QStringLiteral("socket_disconnected"),
                  QStringLiteral("local_socket"),
                  logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"method", method.toStdString()}});
        return;
    }

    const int id = m_nextRequestId++;
    QJsonObject root;
    root["id"] = id;
    root["method"] = method;
    root["params"] = params;

    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    m_socket->write(payload);
    m_socket->flush();

    m_pending.insert(id, PendingRequest{method});

    KLOG_DEBUG(QStringLiteral("KhronicleApiClient"),
               QStringLiteral("sendRequest"),
               QStringLiteral("api_request_sent"),
               QStringLiteral("ui_action"),
               QStringLiteral("json_rpc"),
               logging::defaultWho(),
               QString(),
               nlohmann::json{{"method", method.toStdString()},
                              {"id", id}});
}

void KhronicleApiClient::handleResponse(const QJsonObject &obj)
{
    // Match responses to requests by id and emit QML-friendly signals.
    const int id = obj.value("id").toInt(-1);
    if (!m_pending.contains(id)) {
        return;
    }

    const PendingRequest pending = m_pending.take(id);

    if (obj.contains("error")) {
        emit errorOccurred(obj.value("error").toString());
        KLOG_WARN(QStringLiteral("KhronicleApiClient"),
                  QStringLiteral("handleResponse"),
                  QStringLiteral("api_request_error"),
                  QStringLiteral("daemon_error"),
                  QStringLiteral("json_rpc"),
                  logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"method", pending.method.toStdString()},
                                 {"id", id}});
        return;
    }

    const QJsonValue resultValue = obj.value("result");
    if (!resultValue.isObject()) {
        emit errorOccurred(QStringLiteral("Malformed response result"));
        return;
    }

    const QJsonObject result = resultValue.toObject();

    KLOG_DEBUG(QStringLiteral("KhronicleApiClient"),
               QStringLiteral("handleResponse"),
               QStringLiteral("api_request_completed"),
               QStringLiteral("daemon_response"),
               QStringLiteral("json_rpc"),
               logging::defaultWho(),
               QString(),
               nlohmann::json{{"method", pending.method.toStdString()},
                              {"id", id}});

    if (pending.method == "get_changes_since" || pending.method == "get_changes_between") {
        emit changesLoaded(convertEventsJsonToVariantList(result.value("events")));
        return;
    }

    if (pending.method == "list_snapshots") {
        emit snapshotsLoaded(convertSnapshotsJsonToVariantList(result.value("snapshots")));
        return;
    }

    if (pending.method == "get_snapshot") {
        QJsonArray single;
        if (result.contains("snapshot") && result.value("snapshot").isObject()) {
            single.append(result.value("snapshot"));
        }
        emit snapshotsLoaded(convertSnapshotsJsonToVariantList(single));
        return;
    }

    if (pending.method == "diff_snapshots") {
        emit diffLoaded(convertDiffJsonToVariantList(result.value("diff")));
        return;
    }

    if (pending.method == "explain_change_between") {
        emit explanationLoaded(result.value("summary").toString());
        return;
    }

    if (pending.method == "summary_since") {
        emit summaryLoaded(convertSummaryJsonToVariantMap(result));
        return;
    }
}

QString KhronicleApiClient::socketPath() const
{
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        return runtimeDir + QStringLiteral("/khronicle.sock");
    }
    return QStringLiteral("/run/user/%1/khronicle.sock").arg(getuid());
}

QVariantList KhronicleApiClient::convertEventsJsonToVariantList(
    const QJsonValue &eventsValue) const
{
    QVariantList events;
    if (!eventsValue.isArray()) {
        return events;
    }

    const QJsonArray array = eventsValue.toArray();
    events.reserve(array.size());

    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();

        QVariantMap event;
        event["id"] = obj.value("id").toString();
        event["timestamp"] = obj.value("timestamp").toString();
        event["category"] = obj.value("category").toString();
        event["source"] = obj.value("source").toString();
        event["summary"] = obj.value("summary").toString();
        event["details"] = obj.value("details").toString();

        if (obj.contains("relatedPackages") && obj.value("relatedPackages").isArray()) {
            QVariantList related;
            for (const QJsonValue &pkg : obj.value("relatedPackages").toArray()) {
                related.push_back(pkg.toString());
            }
            event["relatedPackages"] = related;
        }

        events.push_back(event);
    }

    return events;
}

QVariantList KhronicleApiClient::convertSnapshotsJsonToVariantList(
    const QJsonValue &snapshotsValue) const
{
    QVariantList snapshots;
    if (!snapshotsValue.isArray()) {
        return snapshots;
    }

    const QJsonArray array = snapshotsValue.toArray();
    snapshots.reserve(array.size());

    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();

        QVariantMap snapshot;
        snapshot["id"] = obj.value("id").toString();
        snapshot["timestamp"] = obj.value("timestamp").toString();
        snapshot["kernelVersion"] = obj.value("kernelVersion").toString();

        if (obj.contains("keyPackages") && obj.value("keyPackages").isObject()) {
            snapshot["keyPackages"] = obj.value("keyPackages").toObject().toVariantMap();
        }

        snapshots.push_back(snapshot);
    }

    return snapshots;
}

QVariantMap KhronicleApiClient::convertSummaryJsonToVariantMap(
    const QJsonValue &summaryValue) const
{
    if (!summaryValue.isObject()) {
        return {};
    }

    return summaryValue.toObject().toVariantMap();
}

QVariantList KhronicleApiClient::convertDiffJsonToVariantList(
    const QJsonValue &diffValue) const
{
    QVariantList rows;
    if (!diffValue.isObject()) {
        return rows;
    }

    const QJsonObject diff = diffValue.toObject();
    const QJsonValue changedFieldsValue = diff.value("changedFields");
    if (!changedFieldsValue.isArray()) {
        return rows;
    }

    const QJsonArray array = changedFieldsValue.toArray();
    rows.reserve(array.size());

    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject obj = value.toObject();

        QVariantMap row;
        row["path"] = obj.value("path").toString();
        row["before"] = obj.value("before").toString();
        row["after"] = obj.value("after").toString();
        rows.push_back(row);
    }

    return rows;
}

} // namespace khronicle
