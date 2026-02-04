#include "replay/ReplayHarness.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QTemporaryDir>

#include <filesystem>

#include "common/logging.hpp"
#include "common/json_utils.hpp"
#include "daemon/khronicle_api_server.hpp"
#include "daemon/khronicle_daemon.hpp"
#include "daemon/khronicle_store.hpp"
#include "report/ReportCli.hpp"

namespace khronicle {

namespace {

nlohmann::json readJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return nlohmann::json();
    }
    const QByteArray data = file.readAll();
    try {
        return nlohmann::json::parse(data.toStdString());
    } catch (const nlohmann::json::parse_error &) {
        return nlohmann::json();
    }
}

} // namespace

int ReplayHarness::runScenario(const QString &scenarioDir)
{
    const QString scenarioPath = scenarioDir + QDir::separator() + "scenario.json";
    const nlohmann::json scenario = readJsonFile(scenarioPath);
    if (scenario.is_null() || !scenario.contains("steps")) {
        return 1;
    }

    QTemporaryDir replayHome;
    if (!replayHome.isValid()) {
        return 1;
    }

    if (!prepareScenarioDb(scenarioDir, replayHome.path())) {
        return 1;
    }

    qputenv("HOME", replayHome.path().toUtf8());
    qputenv("XDG_RUNTIME_DIR", replayHome.path().toUtf8());
    qputenv("KHRONICLE_LOG_DIR", scenarioDir.toUtf8());
    qputenv("KHRONICLE_REPLAY_NO_SNAPSHOT", "1");

    const QString pacmanPath = scenarioDir + QDir::separator() + "pacman.log";
    if (QFile::exists(pacmanPath)) {
        qputenv("KHRONICLE_PACMAN_LOG_PATH", pacmanPath.toUtf8());
    }
    const QString journalPath = scenarioDir + QDir::separator() + "journal.txt";
    if (QFile::exists(journalPath)) {
        qputenv("KHRONICLE_JOURNAL_PATH", journalPath.toUtf8());
    }

    khronicle::logging::initLogging(QStringLiteral("khronicle-replay"),
                                    qEnvironmentVariableIntValue("KHRONICLE_CODEX_TRACE") == 1);

    KLOG_INFO(QStringLiteral("ReplayHarness"),
              QStringLiteral("runScenario"),
              QStringLiteral("replay_start"),
              QStringLiteral("scenario"),
              QStringLiteral("replay"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"scenarioDir", scenarioDir.toStdString()}}));

    return runSteps(scenario["steps"]);
}

int ReplayHarness::runSteps(const nlohmann::json &steps)
{
    if (!steps.is_array()) {
        return 1;
    }
    for (const auto &step : steps) {
        const std::string action = step.value("action", "");
        if (action == "run_ingestion_cycle") {
            if (runIngestionCycleStep(step) != 0) {
                return 1;
            }
        } else if (action == "api_call") {
            if (runApiStep(step) != 0) {
                return 1;
            }
        } else if (action == "report_cli") {
            if (runReportStep(step) != 0) {
                return 1;
            }
        }
    }
    return 0;
}

int ReplayHarness::runIngestionCycleStep(const nlohmann::json &step)
{
    Q_UNUSED(step)
    KhronicleDaemon daemon;
    daemon.runIngestionCycleForReplay();
    return 0;
}

int ReplayHarness::runApiStep(const nlohmann::json &step)
{
    const nlohmann::json context = step.value("context", nlohmann::json::object());
    const std::string method = context.value("method", "");
    const nlohmann::json params = context.value("params", nlohmann::json::object());

    KhronicleStore store;
    KhronicleApiServer server(store);

    // Use direct method invocation for reliability in test/replay scenarios
    // (socket communication in same thread requires event loop coordination)
    nlohmann::json root;
    root["id"] = 1;
    root["method"] = method;
    root["params"] = params;
    const QByteArray response = server.handleRequestPayload(
        QByteArray::fromStdString(root.dump()));
    const auto parsed = nlohmann::json::parse(response.toStdString(), nullptr, false);
    if (parsed.is_discarded() || parsed.contains("error")) {
        return 1;
    }
    return 0;
}

int ReplayHarness::runReportStep(const nlohmann::json &step)
{
    const nlohmann::json context = step.value("context", nlohmann::json::object());
    const std::string command = context.value("command", "");
    const nlohmann::json args = context.value("args", nlohmann::json::array());

    QStringList argv;
    argv << QStringLiteral("khronicle-report");
    if (!command.empty()) {
        argv << QString::fromStdString(command);
    }
    if (args.is_array()) {
        for (const auto &arg : args) {
            if (arg.is_string()) {
                argv << QString::fromStdString(arg.get<std::string>());
            }
        }
    }

    std::vector<QByteArray> utf8Args;
    std::vector<char *> rawArgs;
    for (const QString &arg : argv) {
        utf8Args.push_back(arg.toLocal8Bit());
    }
    for (auto &arg : utf8Args) {
        rawArgs.push_back(arg.data());
    }

    ReportCli cli;
    return cli.run(rawArgs.size(), rawArgs.data());
}

int ReplayHarness::sendApiRequest(const QString &socketPath,
                                  const QString &method,
                                  const nlohmann::json &params)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath);
    if (!socket.waitForConnected(1000)) {
        return 1;
    }

    nlohmann::json root;
    root["id"] = 1;
    root["method"] = method.toStdString();
    root["params"] = params;

    const QByteArray payload = QByteArray::fromStdString(root.dump());
    socket.write(payload);
    if (!socket.waitForBytesWritten(1000)) {
        return 1;
    }
    if (!socket.waitForReadyRead(1000)) {
        return 1;
    }

    const QByteArray response = socket.readAll();
    const auto parsed = nlohmann::json::parse(response.toStdString(), nullptr, false);
    if (parsed.is_discarded()) {
        return 1;
    }
    if (parsed.contains("error")) {
        return 1;
    }
    return 0;
}

bool ReplayHarness::prepareScenarioDb(const QString &scenarioDir, const QString &replayHome)
{
    const QString srcDb = scenarioDir + QDir::separator() + "db.sqlite";
    if (!QFile::exists(srcDb)) {
        return false;
    }
    const QString targetDir = replayHome + QDir::separator()
        + ".local/share/khronicle";
    QDir().mkpath(targetDir);
    const QString dstDb = targetDir + QDir::separator() + "khronicle.db";
    QFile::remove(dstDb);
    return QFile::copy(srcDb, dstDb);
}

} // namespace khronicle
