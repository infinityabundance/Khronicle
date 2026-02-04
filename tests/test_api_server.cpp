#include <QtTest/QtTest>

#include <QDir>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QCoreApplication>

#include <filesystem>

#include "daemon/khronicle_api_server.hpp"
#include "daemon/khronicle_store.hpp"
#include "common/json_utils.hpp"

class ApiServerTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();

    void testBasicMethods();
    void testErrorHandling();
    void testRulesAndSignals();

private:
    QTemporaryDir m_tempDir;
    QByteArray m_prevHome;
    QByteArray m_prevRuntime;
    void resetDb();
    std::filesystem::path dbPath() const;
    QJsonObject sendRequest(khronicle::KhronicleApiServer &server,
                            const QString &method,
                            const QJsonObject &params = {});
};

void ApiServerTests::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_prevHome = qgetenv("HOME");
    m_prevRuntime = qgetenv("XDG_RUNTIME_DIR");
    qputenv("HOME", m_tempDir.path().toUtf8());
    qputenv("XDG_RUNTIME_DIR", m_tempDir.path().toUtf8());
    QDir().mkpath(m_tempDir.path());
}

void ApiServerTests::cleanupTestCase()
{
    if (m_prevHome.isEmpty()) {
        qunsetenv("HOME");
    } else {
        qputenv("HOME", m_prevHome);
    }

    if (m_prevRuntime.isEmpty()) {
        qunsetenv("XDG_RUNTIME_DIR");
    } else {
        qputenv("XDG_RUNTIME_DIR", m_prevRuntime);
    }

}

std::filesystem::path ApiServerTests::dbPath() const
{
    return std::filesystem::path(m_tempDir.path().toStdString())
        / ".local/share/khronicle/khronicle.db";
}

void ApiServerTests::resetDb()
{
    std::error_code error;
    std::filesystem::remove(dbPath(), error);
}

QJsonObject ApiServerTests::sendRequest(khronicle::KhronicleApiServer &server,
                                        const QString &method,
                                        const QJsonObject &params)
{
    QJsonObject root;
    root["id"] = 1;
    root["method"] = method;
    root["params"] = params;

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);
    const QByteArray response = server.handleRequestPayload(payload);
    if (response.isEmpty()) {
        qWarning() << "Empty response from server";
        return QJsonObject();
    }

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error:" << error.errorString() << "Response:" << response;
        return QJsonObject();
    }
    if (!doc.isObject()) {
        qWarning() << "Response is not an object:" << response;
        return QJsonObject();
    }
    return doc.object();
}

void ApiServerTests::testBasicMethods()
{
    resetDb();

    khronicle::KhronicleStore store;
    khronicle::KhronicleEvent event;
    event.id = "event-1";
    event.timestamp = std::chrono::system_clock::now();
    event.category = khronicle::EventCategory::Kernel;
    event.source = khronicle::EventSource::Pacman;
    event.summary = "kernel";
    event.hostId = store.getHostIdentity().hostId;
    store.addEvent(event);

    khronicle::SystemSnapshot snapshot;
    snapshot.id = "snap-1";
    snapshot.timestamp = std::chrono::system_clock::now();
    snapshot.kernelVersion = "6.1";
    snapshot.hostIdentity = store.getHostIdentity();
    store.addSnapshot(snapshot);

    khronicle::KhronicleApiServer server(store);

    const auto summary = sendRequest(server, "summary_since", {
        {"since", QString::fromStdString(khronicle::toIso8601Utc(event.timestamp - std::chrono::hours(1)))}
    });
    QVERIFY(summary.contains("result"));

    const auto snapshots = sendRequest(server, "list_snapshots");
    QVERIFY(snapshots["result"].toObject().contains("snapshots"));

    const auto diff = sendRequest(server, "diff_snapshots", {
        {"a", "snap-1"},
        {"b", "snap-1"}
    });
    QVERIFY(diff.contains("result"));
}

void ApiServerTests::testErrorHandling()
{
    resetDb();
    khronicle::KhronicleStore store;
    khronicle::KhronicleApiServer server(store);

    const auto response = sendRequest(server, "unknown_method");
    QVERIFY(response.contains("error"));
}

void ApiServerTests::testRulesAndSignals()
{
    resetDb();
    khronicle::KhronicleStore store;
    khronicle::KhronicleApiServer server(store);

    const auto rules = sendRequest(server, "list_watch_rules");
    QVERIFY(rules["result"].toObject().contains("rules"));

    const auto watchSignals = sendRequest(server, "get_watch_signals_since", {
        {"since", QString::fromStdString(khronicle::toIso8601Utc(std::chrono::system_clock::now()))}
    });
    QVERIFY(watchSignals["result"].toObject().contains("signals"));
}

QTEST_MAIN(ApiServerTests)
#include "test_api_server.moc"
