#pragma once

#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QLocalSocket>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

namespace khronicle {

class WatchClient : public QObject
{
    Q_OBJECT
public:
    explicit WatchClient(QObject *parent = nullptr);
    ~WatchClient() override;

    // QML-facing methods to manage watch rules/signals via local JSON-RPC.
    Q_INVOKABLE void loadRules();
    Q_INVOKABLE void saveRule(const QVariantMap &rule);
    Q_INVOKABLE void deleteRule(const QString &id);
    Q_INVOKABLE void loadSignalsSince(const QDateTime &since);

signals:
    void rulesLoaded(const QVariantList &rules);
    void signalsLoaded(const QVariantList &signals);
    void errorOccurred(const QString &message);

private:
    struct PendingRequest {
        QString method;
    };

    QLocalSocket *m_socket = nullptr;
    int m_nextRequestId = 1;
    QHash<int, PendingRequest> m_pending;

    void connectToDaemon();
    void sendRequest(const QString &method, const QJsonObject &params);
    void handleResponse(const QJsonObject &obj);
    QString socketPath() const;
};

} // namespace khronicle
