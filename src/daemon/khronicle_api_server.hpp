#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>

#include <nlohmann/json.hpp>

#include "daemon/khronicle_store.hpp"

namespace khronicle {

/**
 * KhronicleApiServer exposes KhronicleStore data over a local UNIX socket
 * using a minimal JSON-RPC-like protocol.
 */
class KhronicleApiServer : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleApiServer(KhronicleStore &store, QObject *parent = nullptr);
    ~KhronicleApiServer() override;

    // Start listening on /run/user/$UID/khronicle.sock
    bool start();

private slots:
    void handleNewConnection();
    void handleClientReadyRead();

private:
    void handleRequest(QLocalSocket *socket, const QByteArray &payload);
    QByteArray makeErrorResponse(const QString &message, int id = -1) const;
    QByteArray makeResultResponse(const nlohmann::json &result, int id) const;

    KhronicleStore &m_store;
    QLocalServer m_server;
};

} // namespace khronicle
