#include <QCoreApplication>
#include <QCommandLineParser>

#include "replay/ReplayHarness.hpp"
#include "common/logging.hpp"

#include <nlohmann/json.hpp>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("khronicle-replay"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption codexOption(QStringList() << "codex-trace",
                                   "Enable verbose Codex trace logging.");
    parser.addOption(codexOption);
    parser.addPositionalArgument("scenarioDir", "Path to scenario directory.");
    parser.process(app);

    const bool codexTrace = parser.isSet(codexOption)
        || qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1;
    khronicle::logging::initLogging(QStringLiteral("khronicle-replay"), codexTrace);

    const QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        return 1;
    }

    khronicle::ReplayHarness harness;
    return harness.runScenario(args.first());
}
