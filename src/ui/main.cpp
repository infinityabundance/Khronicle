#include <QCoreApplication>
#include <memory>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>

#include "ui/backend/KhronicleApiClient.hpp"
#include "ui/backend/FleetModel.hpp"
#include "ui/backend/WatchClient.hpp"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption fleetOption(QStringList() << "fleet",
                                   "Open in fleet mode with aggregate JSON file.",
                                   "path");
    parser.addOption(fleetOption);
    parser.process(app);

    QUrl url;
    std::unique_ptr<khronicle::FleetModel> fleetModel;
    std::unique_ptr<khronicle::KhronicleApiClient> apiClient;
    std::unique_ptr<khronicle::WatchClient> watchClient;

    if (parser.isSet(fleetOption)) {
        // Fleet mode is offline and read-only: it loads aggregate JSON directly.
        fleetModel = std::make_unique<khronicle::FleetModel>();
        fleetModel->loadAggregateFile(parser.value(fleetOption));
        engine.rootContext()->setContextProperty(QStringLiteral("fleetModel"),
                                                 fleetModel.get());
        url = QUrl::fromLocalFile(
            QStringLiteral(KHRONICLE_QML_DIR "/FleetMain.qml"));
    } else {
        // Normal mode connects to the daemon's local JSON-RPC API.
        apiClient = std::make_unique<khronicle::KhronicleApiClient>();
        engine.rootContext()->setContextProperty(QStringLiteral("khronicleApi"),
                                                 apiClient.get());
        watchClient = std::make_unique<khronicle::WatchClient>();
        engine.rootContext()->setContextProperty(QStringLiteral("watchClient"),
                                                 watchClient.get());
        url = QUrl::fromLocalFile(
            QStringLiteral(KHRONICLE_QML_DIR "/Main.qml"));
    }

    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreated,
        &app,
        [url](QObject *obj, const QUrl &objUrl) {
            if (!obj && url == objUrl) {
                QCoreApplication::exit(EXIT_FAILURE);
            }
        },
        Qt::QueuedConnection);

    engine.load(url);

    return app.exec();
}
