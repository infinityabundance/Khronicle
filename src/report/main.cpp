#include <QCoreApplication>

#include "report/ReportCli.hpp"
#include "common/logging.hpp"
#include "debug/scenario_capture.hpp"

#include <nlohmann/json.hpp>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    bool codexTrace = qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1;
    QStringList filteredArgs;
    filteredArgs.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--codex-trace")) {
            codexTrace = true;
            continue;
        }
        filteredArgs.push_back(arg);
    }
    khronicle::logging::initLogging(QStringLiteral("khronicle-report"), codexTrace);
    KLOG_INFO(QStringLiteral("main"),
              QStringLiteral("main"),
              QStringLiteral("report_cli_start"),
              QStringLiteral("user_invocation"),
              QStringLiteral("cli"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"args", filteredArgs.size()}}));

    if (qEnvironmentVariableIntValue("KHRONICLE_SCENARIO_CAPTURE") == 1) {
        const QString scenarioId = qEnvironmentVariable("KHRONICLE_SCENARIO_ID");
        const QString title = qEnvironmentVariable("KHRONICLE_SCENARIO_TITLE");
        const QString desc = qEnvironmentVariable("KHRONICLE_SCENARIO_DESC");
        if (qEnvironmentVariable("KHRONICLE_SCENARIO_ENTRY").isEmpty()) {
            qputenv("KHRONICLE_SCENARIO_ENTRY", "report_cli");
        }
        khronicle::ScenarioCapture::start(scenarioId, title, desc);
    }

    // CLI entry point: delegate to ReportCli for argument parsing and output.
    khronicle::ReportCli cli;
    std::vector<QByteArray> utf8Args;
    std::vector<char *> rawArgs;
    for (const QString &arg : filteredArgs) {
        utf8Args.push_back(arg.toLocal8Bit());
    }
    for (auto &arg : utf8Args) {
        rawArgs.push_back(arg.data());
    }
    return cli.run(rawArgs.size(), rawArgs.data());
}
