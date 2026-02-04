#include "daemon/watch_engine.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <exception>
#include <random>
#include <sstream>

#include "common/json_utils.hpp"
#include "common/logging.hpp"

namespace khronicle {

namespace {

constexpr auto kRulesReloadInterval = std::chrono::seconds(60);

std::string generateUuid()
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;

    const uint64_t part1 = dist(gen);
    const uint64_t part2 = dist(gen);

    std::ostringstream out;
    out << std::hex;
    out << (part1 >> 32);
    out << "-";
    out << ((part1 >> 16) & 0xFFFF);
    out << "-";
    out << (part1 & 0xFFFF);
    out << "-";
    out << (part2 >> 48);
    out << "-";
    out << (part2 & 0xFFFFFFFFFFFFULL);
    return out.str();
}

std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

int riskRank(const std::string &risk)
{
    const std::string lowered = toLower(risk);
    if (lowered == "critical") {
        return 2;
    }
    if (lowered == "important") {
        return 1;
    }
    return 0;
}

std::string extractRiskLevel(const nlohmann::json &state)
{
    if (!state.is_object()) {
        return {};
    }
    auto it = state.find("riskLevel");
    if (it != state.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return {};
}

std::string eventRiskLevel(const KhronicleEvent &event)
{
    std::string risk = extractRiskLevel(event.afterState);
    if (risk.empty()) {
        risk = extractRiskLevel(event.beforeState);
    }
    return risk;
}

bool containsCaseInsensitive(const std::string &value, const std::string &needle)
{
    if (needle.empty()) {
        return true;
    }
    const std::string lowerValue = toLower(value);
    const std::string lowerNeedle = toLower(needle);
    return lowerValue.find(lowerNeedle) != std::string::npos;
}

bool listContainsSubstring(const std::vector<std::string> &values,
                           const std::string &needle)
{
    if (needle.empty()) {
        return true;
    }
    for (const auto &value : values) {
        if (containsCaseInsensitive(value, needle)) {
            return true;
        }
    }
    return false;
}

bool jsonKeysContainSubstring(const nlohmann::json &obj,
                              const std::string &needle)
{
    if (needle.empty()) {
        return true;
    }
    if (!obj.is_object()) {
        return false;
    }
    for (const auto &item : obj.items()) {
        if (containsCaseInsensitive(item.key(), needle)) {
            return true;
        }
        if (item.value().is_string()
            && containsCaseInsensitive(item.value().get<std::string>(), needle)) {
            return true;
        }
    }
    return false;
}

} // namespace

WatchEngine::WatchEngine(KhronicleStore &store)
    : m_store(store)
    , m_lastRulesReload(std::chrono::system_clock::time_point{})
{
}

void WatchEngine::evaluateEvent(const KhronicleEvent &event)
{
    // Evaluate all enabled event-scope rules against this event and persist
    // WatchSignal records for any matches.
    KLOG_DEBUG(QStringLiteral("WatchEngine"),
               QStringLiteral("evaluateEvent"),
               QStringLiteral("evaluate_watch_rules"),
               QStringLiteral("ingestion_event_interpretation"),
               QStringLiteral("rule_match"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json{{"eventId", event.id},
                              {"rulesCached", m_rulesCache.size()}});
    maybeReloadRules();

    for (const auto &rule : m_rulesCache) {
        if (!rule.enabled || rule.scope != WatchScope::Event) {
            continue;
        }
        if (!ruleMatchesEvent(rule, event)) {
            continue;
        }

        WatchSignal signal;
        signal.id = generateUuid();
        signal.timestamp = event.timestamp;
        signal.ruleId = rule.id;
        signal.ruleName = rule.name;
        signal.severity = rule.severity;
        signal.originType = "event";
        signal.originId = event.id;
        signal.message = "Rule '" + rule.name + "' matched event";

        if (!rule.categoryEquals.empty()) {
            signal.message += " category '" + rule.categoryEquals + "'";
        }

        KLOG_INFO(QStringLiteral("WatchEngine"),
                  QStringLiteral("evaluateEvent"),
                  QStringLiteral("watch_signal_fired"),
                  QStringLiteral("rule_match"),
                  QStringLiteral("persist_signal"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"ruleId", rule.id},
                                 {"originId", event.id},
                                 {"severity", toWatchSeverityString(rule.severity)}});
        m_store.addWatchSignal(signal);
    }
}

void WatchEngine::evaluateSnapshot(const SystemSnapshot &snapshot)
{
    // Evaluate all enabled snapshot-scope rules against this snapshot.
    KLOG_DEBUG(QStringLiteral("WatchEngine"),
               QStringLiteral("evaluateSnapshot"),
               QStringLiteral("evaluate_watch_rules"),
               QStringLiteral("snapshot_interpretation"),
               QStringLiteral("rule_match"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json{{"snapshotId", snapshot.id},
                              {"rulesCached", m_rulesCache.size()}});
    maybeReloadRules();

    for (const auto &rule : m_rulesCache) {
        if (!rule.enabled || rule.scope != WatchScope::Snapshot) {
            continue;
        }
        if (!ruleMatchesSnapshot(rule, snapshot)) {
            continue;
        }

        WatchSignal signal;
        signal.id = generateUuid();
        signal.timestamp = snapshot.timestamp;
        signal.ruleId = rule.id;
        signal.ruleName = rule.name;
        signal.severity = rule.severity;
        signal.originType = "snapshot";
        signal.originId = snapshot.id;
        signal.message = "Rule '" + rule.name + "' matched snapshot";

        if (!rule.categoryEquals.empty()) {
            signal.message += " category '" + rule.categoryEquals + "'";
        }

        KLOG_INFO(QStringLiteral("WatchEngine"),
                  QStringLiteral("evaluateSnapshot"),
                  QStringLiteral("watch_signal_fired"),
                  QStringLiteral("rule_match"),
                  QStringLiteral("persist_signal"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json{{"ruleId", rule.id},
                                 {"originId", snapshot.id},
                                 {"severity", toWatchSeverityString(rule.severity)}});
        m_store.addWatchSignal(signal);
    }
}

void WatchEngine::maybeReloadRules()
{
    // Cache rules to keep ingestion cycles fast. Reload periodically.
    const auto now = std::chrono::system_clock::now();
    if (!m_rulesCache.empty()
        && now - m_lastRulesReload < kRulesReloadInterval) {
        return;
    }

    m_rulesCache = m_store.listWatchRules();
    m_lastRulesReload = now;
    KLOG_DEBUG(QStringLiteral("WatchEngine"),
               QStringLiteral("maybeReloadRules"),
               QStringLiteral("rules_cache_reload"),
               QStringLiteral("periodic_refresh"),
               QStringLiteral("sqlite_query"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json{{"ruleCount", m_rulesCache.size()}});
}

bool WatchEngine::ruleMatchesEvent(const WatchRule &rule,
                                   const KhronicleEvent &event) const
{
    // Declarative, transparent matching rules. No scripting or code execution.
    if (!withinActiveWindow(rule, event.timestamp)) {
        return false;
    }

    if (!rule.categoryEquals.empty()) {
        const std::string expected = toLower(rule.categoryEquals);
        const std::string actual = toLower(toCategoryString(event.category));
        if (expected != actual) {
            return false;
        }
    }

    if (!rule.riskLevelAtLeast.empty()) {
        const std::string eventRisk = eventRiskLevel(event);
        if (riskRank(eventRisk) < riskRank(rule.riskLevelAtLeast)) {
            return false;
        }
    }

    if (!rule.packageNameContains.empty()) {
        if (!listContainsSubstring(event.relatedPackages, rule.packageNameContains)) {
            return false;
        }
    }

    return true;
}

bool WatchEngine::ruleMatchesSnapshot(const WatchRule &rule,
                                      const SystemSnapshot &snapshot) const
{
    // Snapshot rules match against the snapshot's summarized state (key packages,
    // firmware versions, etc.) rather than specific events.
    if (!withinActiveWindow(rule, snapshot.timestamp)) {
        return false;
    }

    if (!rule.categoryEquals.empty()) {
        const std::string expected = toLower(rule.categoryEquals);
        bool matches = false;
        if (expected == "kernel") {
            matches = !snapshot.kernelVersion.empty();
        } else if (expected == "gpu_driver" || expected == "gpu") {
            matches = snapshot.gpuDriver.is_object() && !snapshot.gpuDriver.empty();
        } else if (expected == "firmware") {
            matches = snapshot.firmwareVersions.is_object()
                && !snapshot.firmwareVersions.empty();
        } else if (expected == "package") {
            matches = snapshot.keyPackages.is_object()
                && !snapshot.keyPackages.empty();
        } else if (expected == "system") {
            matches = true;
        }
        if (!matches) {
            return false;
        }
    }

    if (!rule.riskLevelAtLeast.empty()) {
        const std::string snapshotRisk =
            extractRiskLevel(snapshot.keyPackages);
        if (riskRank(snapshotRisk) < riskRank(rule.riskLevelAtLeast)) {
            return false;
        }
    }

    if (!rule.packageNameContains.empty()) {
        if (!jsonKeysContainSubstring(snapshot.keyPackages,
                                      rule.packageNameContains)) {
            return false;
        }
    }

    return true;
}

bool WatchEngine::withinActiveWindow(const WatchRule &rule,
                                     std::chrono::system_clock::time_point t) const
{
    // Active windows define a local-time maintenance window. Rules apply only
    // outside that window (i.e., inside = safe/maintenance time).
    if (rule.activeFrom.empty() || rule.activeTo.empty()) {
        return true;
    }

    auto parseTime = [](const std::string &value, int &minutesOut) -> bool {
        if (value.size() != 5 || value[2] != ':') {
            return false;
        }
        int hours = 0;
        int minutes = 0;
        try {
            hours = std::stoi(value.substr(0, 2));
            minutes = std::stoi(value.substr(3, 2));
        } catch (const std::exception &) {
            return false;
        }
        if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
            return false;
        }
        minutesOut = hours * 60 + minutes;
        return true;
    };

    int startMinutes = 0;
    int endMinutes = 0;
    if (!parseTime(rule.activeFrom, startMinutes)
        || !parseTime(rule.activeTo, endMinutes)) {
        return true;
    }

    std::time_t raw = std::chrono::system_clock::to_time_t(t);
    std::tm localTime{};
#if defined(_WIN32)
    localtime_s(&localTime, &raw);
#else
    localtime_r(&raw, &localTime);
#endif
    const int currentMinutes = localTime.tm_hour * 60 + localTime.tm_min;

    bool inWindow = false;
    if (startMinutes <= endMinutes) {
        inWindow = currentMinutes >= startMinutes && currentMinutes < endMinutes;
    } else {
        inWindow = currentMinutes >= startMinutes || currentMinutes < endMinutes;
    }

    // If an active window is defined, rules apply outside that window
    // (maintenance window is treated as safe time).
    return !inWindow;
}

} // namespace khronicle
