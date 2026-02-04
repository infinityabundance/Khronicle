#pragma once

#include <QString>

#include <nlohmann/json.hpp>

namespace khronicle::logging {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error
};

// Initialize logging for the current process. Call early in main().
void initLogging(const QString &processName, bool codexTraceEnabled);

bool isCodexTraceEnabled();

// Thread-local correlation support for linking related log events.
void setCorrelationId(const QString &corrId);
QString currentCorrelationId();

class CorrelationScope {
public:
    explicit CorrelationScope(const QString &corrId);
    ~CorrelationScope();

private:
    QString m_prev;
};

// Structured log event. All fields are required; use empty strings where unknown.
void logEvent(LogLevel level,
              const QString &processName,
              const QString &component,
              const QString &where,
              const QString &what,
              const QString &why,
              const QString &how,
              const QString &who,
              const QString &correlationId,
              const nlohmann::json &context = nlohmann::json::object());

QString defaultProcessName();
QString defaultWho();

} // namespace khronicle::logging

#define KLOG_DEBUG(component, where, what, why, how, who, corr, ctxJson) \
    ::khronicle::logging::logEvent(::khronicle::logging::LogLevel::Debug, \
                                   ::khronicle::logging::defaultProcessName(), \
                                   (component), (where), (what), (why), (how), (who), (corr), (ctxJson))

#define KLOG_INFO(component, where, what, why, how, who, corr, ctxJson) \
    ::khronicle::logging::logEvent(::khronicle::logging::LogLevel::Info, \
                                   ::khronicle::logging::defaultProcessName(), \
                                   (component), (where), (what), (why), (how), (who), (corr), (ctxJson))

#define KLOG_WARN(component, where, what, why, how, who, corr, ctxJson) \
    ::khronicle::logging::logEvent(::khronicle::logging::LogLevel::Warn, \
                                   ::khronicle::logging::defaultProcessName(), \
                                   (component), (where), (what), (why), (how), (who), (corr), (ctxJson))

#define KLOG_ERROR(component, where, what, why, how, who, corr, ctxJson) \
    ::khronicle::logging::logEvent(::khronicle::logging::LogLevel::Error, \
                                   ::khronicle::logging::defaultProcessName(), \
                                   (component), (where), (what), (why), (how), (who), (corr), (ctxJson))
