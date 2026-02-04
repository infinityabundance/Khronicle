#include <QCoreApplication>
#include <QDebug>

#include <nlohmann/json.hpp>

#include "daemon/khronicle_daemon.hpp"
#include "common/logging.hpp"
#include "debug/scenario_capture.hpp"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("khronicle-daemon"));
    qInfo() << "Khronicle daemon starting...";

    bool codexTrace = qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1;
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--codex-trace")) {
            codexTrace = true;
        }
    }
    khronicle::logging::initLogging(QStringLiteral("khronicle-daemon"), codexTrace);
    KLOG_INFO(QStringLiteral("main"),
              QStringLiteral("main"),
              QStringLiteral("daemon_start"),
              QStringLiteral("user_start"),
              QStringLiteral("default_config"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());

    if (qEnvironmentVariableIntValue("KHRONICLE_SCENARIO_CAPTURE") == 1) {
        const QString scenarioId = qEnvironmentVariable("KHRONICLE_SCENARIO_ID");
        const QString title = qEnvironmentVariable("KHRONICLE_SCENARIO_TITLE");
        const QString desc = qEnvironmentVariable("KHRONICLE_SCENARIO_DESC");
        if (qEnvironmentVariable("KHRONICLE_SCENARIO_ENTRY").isEmpty()) {
            qputenv("KHRONICLE_SCENARIO_ENTRY", "daemon_ingestion_cycle");
        }
        khronicle::ScenarioCapture::start(scenarioId, title, desc);
    }

    // The daemon lives for the lifetime of the process.
    khronicle::KhronicleDaemon daemon;
    daemon.start();

    return app.exec();
}
