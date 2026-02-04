#include "debug/scenario_capture.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>

#include <filesystem>
#include <mutex>

#include "common/logging.hpp"
#include "common/json_utils.hpp"

namespace khronicle {

namespace {

std::mutex g_mutex;
bool g_enabled = false;
QString g_scenarioDir;
nlohmann::json g_scenario;

QString baseScenariosDir()
{
    const QString home = qEnvironmentVariable("HOME");
    if (home.isEmpty()) {
        return QStringLiteral(".local/share/khronicle/scenarios");
    }
    return home + QStringLiteral("/.local/share/khronicle/scenarios");
}

QString dbPathFromHome()
{
    const QString home = qEnvironmentVariable("HOME");
    if (home.isEmpty()) {
        return QStringLiteral(".local/share/khronicle/khronicle.db");
    }
    return home + QStringLiteral("/.local/share/khronicle/khronicle.db");
}

void writeScenarioJson()
{
    if (g_scenarioDir.isEmpty()) {
        return;
    }
    const QString path = g_scenarioDir + QDir::separator() + "scenario.json";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    file.write(QString::fromStdString(g_scenario.dump(2)).toUtf8());
}

} // namespace

bool ScenarioCapture::isEnabled()
{
    return g_enabled;
}

void ScenarioCapture::start(const QString &scenarioId,
                            const QString &title,
                            const QString &description)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_enabled) {
        return;
    }

    if (scenarioId.isEmpty()) {
        return;
    }

    g_enabled = true;
    g_scenarioDir = baseScenariosDir() + QDir::separator() + scenarioId;
    QDir().mkpath(g_scenarioDir);

    g_scenario = nlohmann::json::object();
    g_scenario["id"] = scenarioId.toStdString();
    g_scenario["title"] = title.toStdString();
    g_scenario["description"] = description.toStdString();
    g_scenario["khronicleVersion"] =
        QCoreApplication::applicationVersion().toStdString();
    const QString entryPoint = qEnvironmentVariable("KHRONICLE_SCENARIO_ENTRY");
    g_scenario["entryPoint"] = entryPoint.isEmpty()
        ? "unknown"
        : entryPoint.toStdString();
    g_scenario["steps"] = nlohmann::json::array();

    const QString dbPath = dbPathFromHome();
    const QString targetDb = g_scenarioDir + QDir::separator() + "db.sqlite";
    if (QFile::exists(dbPath)) {
        QFile::remove(targetDb);
        QFile::copy(dbPath, targetDb);
    }

    writeScenarioJson();

    KLOG_INFO(QStringLiteral("ScenarioCapture"),
              QStringLiteral("start"),
              QStringLiteral("scenario_start"),
              QStringLiteral("capture_enabled"),
              QStringLiteral("copy_db"),
              khronicle::logging::defaultWho(),
              QString(),
              (nlohmann::json{{"scenarioId", scenarioId.toStdString()},
                             {"dir", g_scenarioDir.toStdString()}}));
}

void ScenarioCapture::recordStep(const nlohmann::json &step)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) {
        return;
    }
    g_scenario["steps"].push_back(step);
    writeScenarioJson();
}

void ScenarioCapture::finalize()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_enabled) {
        return;
    }
    g_enabled = false;
    writeScenarioJson();
}

QString ScenarioCapture::scenarioDir()
{
    return g_scenarioDir;
}

} // namespace khronicle
