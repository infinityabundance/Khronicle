#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "common/models.hpp"
#include "daemon/khronicle_store.hpp"

namespace khronicle {

// WatchEngine evaluates declarative watch rules against events and snapshots.
// It records matches as WatchSignal entries in the store.
class WatchEngine {
public:
    explicit WatchEngine(KhronicleStore &store);

    void evaluateEvent(const KhronicleEvent &event);
    void evaluateSnapshot(const SystemSnapshot &snapshot);

private:
    KhronicleStore &m_store;

    std::vector<WatchRule> m_rulesCache;
    std::chrono::system_clock::time_point m_lastRulesReload;

    void maybeReloadRules();
    bool ruleMatchesEvent(const WatchRule &rule, const KhronicleEvent &event) const;
    bool ruleMatchesSnapshot(const WatchRule &rule, const SystemSnapshot &snapshot) const;
    bool withinActiveWindow(const WatchRule &rule,
                            std::chrono::system_clock::time_point t) const;
};

} // namespace khronicle
