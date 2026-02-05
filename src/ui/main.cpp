#include <QCoreApplication>
#include <memory>
#include <chrono>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QThread>

#include "ui/backend/KhronicleApiClient.hpp"
#include "ui/backend/DaemonController.hpp"
#include "ui/backend/FleetModel.hpp"
#include "ui/backend/WatchClient.hpp"
#include "common/logging.hpp"
#include "common/process_utils.hpp"
#include <nlohmann/json.hpp>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    QQuickStyle::setStyle(QStringLiteral("org.kde.desktop"));

    QQmlApplicationEngine engine;
    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption codexOption(QStringList() << "codex-trace",
                                   "Enable verbose Codex trace logging.");
    QCommandLineOption fleetOption(QStringList() << "fleet",
                                   "Open in fleet mode with aggregate JSON file.",
                                   "path");
    parser.addOption(codexOption);
    parser.addOption(fleetOption);
    parser.process(app);

    QUrl url;
    std::unique_ptr<khronicle::FleetModel> fleetModel;
    std::unique_ptr<khronicle::KhronicleApiClient> apiClient;
    std::unique_ptr<khronicle::WatchClient> watchClient;
    std::unique_ptr<khronicle::DaemonController> daemonController;

    const bool codexTrace = parser.isSet(codexOption)
        || qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1;
    khronicle::logging::initLogging(QStringLiteral("khronicle"), codexTrace);

    const QString iconPath = khronicle::appIconPath();
    if (!iconPath.isEmpty()) {
        app.setWindowIcon(QIcon(iconPath));
    }

    KLOG_INFO(QStringLiteral("main"),
              QStringLiteral("main"),
              QStringLiteral("ui_start"),
              QStringLiteral("user_start"),
              QStringLiteral("qt_app"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"fleetMode", parser.isSet(fleetOption)}}));

    if (!parser.isSet(fleetOption)) {
        if (!khronicle::isDaemonRunning()) {
            KLOG_INFO(QStringLiteral("main"),
                      QStringLiteral("main"),
                      QStringLiteral("auto_start_daemon"),
                      QStringLiteral("ui_start"),
                      QStringLiteral("best_effort"),
                      khronicle::logging::defaultWho(),
                      QString(),
                      nlohmann::json::object());
            khronicle::startDaemon();
            const auto start = std::chrono::steady_clock::now();
            while (!khronicle::isDaemonRunning()) {
                if (std::chrono::steady_clock::now() - start
                    > std::chrono::seconds(5)) {
                    break;
                }
                QThread::msleep(100);
            }
        }

        const bool launchedFromTray =
            qEnvironmentVariableIntValue("KHRONICLE_LAUNCHED_FROM_TRAY") == 1;
        const bool trayAlreadyRunning = khronicle::isTrayRunning();
        if (qEnvironmentVariableIntValue("KHRONICLE_NO_TRAY_ON_START") != 1
            && !launchedFromTray
            && !trayAlreadyRunning
            && khronicle::isDaemonRunning()) {
            KLOG_INFO(QStringLiteral("main"),
                      QStringLiteral("main"),
                      QStringLiteral("auto_start_tray"),
                      QStringLiteral("ui_start"),
                      QStringLiteral("best_effort"),
                      khronicle::logging::defaultWho(),
                      QString(),
                      nlohmann::json::object());
            khronicle::startTray();
        }
    }

    if (parser.isSet(fleetOption)) {
        // Fleet mode is offline and read-only: it loads aggregate JSON directly.
        fleetModel = std::make_unique<khronicle::FleetModel>();
        fleetModel->loadAggregateFile(parser.value(fleetOption));
        engine.rootContext()->setContextProperty(QStringLiteral("fleetModel"),
                                                 fleetModel.get());
        engine.rootContext()->setContextProperty(QStringLiteral("khronicleIconPath"),
                                                 iconPath);
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
        daemonController = std::make_unique<khronicle::DaemonController>();
        engine.rootContext()->setContextProperty(QStringLiteral("daemonController"),
                                                 daemonController.get());
        engine.rootContext()->setContextProperty(QStringLiteral("khronicleIconPath"),
                                                 iconPath);
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
