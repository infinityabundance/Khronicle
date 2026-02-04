#pragma once

#include <QObject>
#include <QLocalSocket>
#include <QDateTime>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>

namespace khronicle {

/**
 * KhronicleApiClient is a thin client used by the QML UI
 * to communicate with the Khronicle daemon via the local JSON-RPC API.
 *
 * It exposes high-level, QML-friendly methods and emits signals with
 * QVariant-based data structures.
 */
class KhronicleApiClient : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleApiClient(QObject *parent = nullptr);
    ~KhronicleApiClient() override;

    // Connect to /run/user/$UID/khronicle.sock and keep a QLocalSocket open.
    Q_INVOKABLE void connectToDaemon();

    // API calls exposed to QML (fire-and-forget, results via signals).
    Q_INVOKABLE void loadChangesSince(const QDateTime &since);
    Q_INVOKABLE void loadChangesBetween(const QDateTime &from, const QDateTime &to);
    Q_INVOKABLE void loadSummarySince(const QDateTime &since);
    Q_INVOKABLE void loadSnapshots();
    Q_INVOKABLE void loadDiff(const QString &snapshotAId, const QString &snapshotBId);
    Q_INVOKABLE void loadExplanationBetween(const QDateTime &from, const QDateTime &to);

signals:
    void connectedChanged(bool connected);

    // Emitted when corresponding results arrive from the daemon:
    void changesLoaded(const QVariantList &events);
    void summaryLoaded(const QVariantMap &summary);
    void snapshotsLoaded(const QVariantList &snapshots);
    void diffLoaded(const QVariantList &diffRows);
    void explanationLoaded(const QString &summary);

    // For debug / error reporting in QML:
    void errorOccurred(const QString &message);

private slots:
    void onSocketConnected();
    void onSocketError(QLocalSocket::LocalSocketError error);
    void onSocketReadyRead();

private:
    struct PendingRequest {
        QString method;
    };

    QLocalSocket *m_socket;
    int m_nextRequestId;
    QHash<int, PendingRequest> m_pending;
    bool m_connected = false;

    void sendRequest(const QString &method, const QJsonObject &params);
    void handleResponse(const QJsonObject &obj);
    QString socketPath() const;

    // Helpers to convert JSON payloads into QVariant structures for QML:
    QVariantList convertEventsJsonToVariantList(const QJsonValue &eventsValue) const;
    QVariantList convertSnapshotsJsonToVariantList(const QJsonValue &snapshotsValue) const;
    QVariantMap convertSummaryJsonToVariantMap(const QJsonValue &summaryValue) const;
    QVariantList convertDiffJsonToVariantList(const QJsonValue &diffValue) const;
};

} // namespace khronicle
