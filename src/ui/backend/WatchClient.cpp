#include "ui/backend/WatchClient.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

#include <unistd.h>

namespace khronicle {

namespace {

QString toIso8601Utc(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

} // namespace

WatchClient::WatchClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QLocalSocket(this))
{
    connect(m_socket, &QLocalSocket::errorOccurred, this,
            [this](QLocalSocket::LocalSocketError) {
                emit errorOccurred(m_socket->errorString());
            });
    connect(m_socket, &QLocalSocket::readyRead, this, [this]() {
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
    });
}

WatchClient::~WatchClient() = default;

void WatchClient::connectToDaemon()
{
    if (m_socket->state() == QLocalSocket::ConnectedState) {
        return;
    }
    m_socket->connectToServer(socketPath());
}

void WatchClient::loadRules()
{
    sendRequest(QStringLiteral("list_watch_rules"), QJsonObject());
}

void WatchClient::saveRule(const QVariantMap &rule)
{
    QVariantMap payload = rule;
    if (!payload.contains(QStringLiteral("id"))
        || payload.value(QStringLiteral("id")).toString().isEmpty()) {
        payload[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    QJsonObject params;
    params["rule"] = QJsonObject::fromVariantMap(payload);
    sendRequest(QStringLiteral("upsert_watch_rule"), params);
}

void WatchClient::deleteRule(const QString &id)
{
    QJsonObject params;
    params["id"] = id;
    sendRequest(QStringLiteral("delete_watch_rule"), params);
}

void WatchClient::loadSignalsSince(const QDateTime &since)
{
    QJsonObject params;
    params["since"] = toIso8601Utc(since);
    sendRequest(QStringLiteral("get_watch_signals_since"), params);
}

void WatchClient::sendRequest(const QString &method, const QJsonObject &params)
{
    // WatchClient mirrors KhronicleApiClient but only for rule/signal endpoints.
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        connectToDaemon();
        emit errorOccurred(QStringLiteral("Not connected to Khronicle daemon"));
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
}

void WatchClient::handleResponse(const QJsonObject &obj)
{
    // Resolve request IDs and emit QML-friendly data.
    const int id = obj.value("id").toInt(-1);
    if (!m_pending.contains(id)) {
        return;
    }

    const PendingRequest pending = m_pending.take(id);
    if (obj.contains("error")) {
        emit errorOccurred(obj.value("error").toString());
        return;
    }

    const QJsonValue resultValue = obj.value("result");
    if (!resultValue.isObject()) {
        emit errorOccurred(QStringLiteral("Malformed response result"));
        return;
    }

    const QJsonObject result = resultValue.toObject();
    if (pending.method == "list_watch_rules") {
        emit rulesLoaded(result.value("rules").toArray().toVariantList());
        return;
    }

    if (pending.method == "get_watch_signals_since") {
        emit signalsLoaded(result.value("signals").toArray().toVariantList());
        return;
    }
}

QString WatchClient::socketPath() const
{
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        return runtimeDir + QStringLiteral("/khronicle.sock");
    }
    return QStringLiteral("/run/user/%1/khronicle.sock").arg(getuid());
}

} // namespace khronicle
