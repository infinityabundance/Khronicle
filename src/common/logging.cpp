#include "common/logging.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <unistd.h>

#include <mutex>

namespace khronicle::logging {

namespace {

constexpr qint64 kMaxLogSizeBytes = 5 * 1024 * 1024;

std::mutex g_logMutex;
bool g_codexTraceEnabled = false;
QString g_processName;

thread_local QString t_corrId;

QString levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warn:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString logsDirPath()
{
    const QString home = qEnvironmentVariable("HOME");
    if (home.isEmpty()) {
        return QStringLiteral(".local/share/khronicle/logs");
    }
    return home + QStringLiteral("/.local/share/khronicle/logs");
}

QString logFilePath(const QString &processName, const QString &suffix)
{
    const QString base = processName.isEmpty()
        ? QStringLiteral("khronicle")
        : processName;
    return logsDirPath() + QDir::separator() + base + suffix;
}

void rotateIfNeeded(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists() || info.size() < kMaxLogSizeBytes) {
        return;
    }

    const QString rotated = path + QStringLiteral(".1");
    QFile::remove(rotated);
    QFile::rename(path, rotated);
}

void writeLine(const QString &path, const QString &line)
{
    QDir().mkpath(logsDirPath());
    rotateIfNeeded(path);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr, "%s\n", line.toUtf8().constData());
        return;
    }

    file.write(line.toUtf8());
    file.write("\n");
}

QString threadIdString()
{
    return QStringLiteral("0x%1")
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
}

} // namespace

void initLogging(const QString &processName, bool codexTraceEnabled)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_processName = processName;
    g_codexTraceEnabled = codexTraceEnabled;
}

bool isCodexTraceEnabled()
{
    return g_codexTraceEnabled;
}

void setCorrelationId(const QString &corrId)
{
    t_corrId = corrId;
}

QString currentCorrelationId()
{
    return t_corrId;
}

CorrelationScope::CorrelationScope(const QString &corrId)
    : m_prev(t_corrId)
{
    t_corrId = corrId;
}

CorrelationScope::~CorrelationScope()
{
    t_corrId = m_prev;
}

QString defaultProcessName()
{
    if (!g_processName.isEmpty()) {
        return g_processName;
    }
    if (QCoreApplication::instance()) {
        const QString appName = QCoreApplication::applicationName();
        if (!appName.isEmpty()) {
            return appName;
        }
    }
    return QStringLiteral("khronicle");
}

QString defaultWho()
{
    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        hostname[0] = '\0';
    }
    return QStringLiteral("host:%1,uid:%2")
        .arg(QString::fromUtf8(hostname))
        .arg(static_cast<int>(getuid()));
}

void logEvent(LogLevel level,
              const QString &processName,
              const QString &component,
              const QString &where,
              const QString &what,
              const QString &why,
              const QString &how,
              const QString &who,
              const QString &correlationId,
              const nlohmann::json &context)
{
    const QString corr = correlationId.isEmpty() ? currentCorrelationId() : correlationId;
    nlohmann::json payload = {
        {"ts", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString()},
        {"level", levelToString(level).toStdString()},
        {"process", processName.toStdString()},
        {"thread", threadIdString().toStdString()},
        {"component", component.toStdString()},
        {"where", where.toStdString()},
        {"what", what.toStdString()},
        {"why", why.toStdString()},
        {"how", how.toStdString()},
        {"who", who.toStdString()},
        {"corr", corr.toStdString()},
        {"context", context}
    };

    const QString line = QString::fromStdString(payload.dump());

    const QString process = processName.isEmpty() ? defaultProcessName() : processName;
    const QString mainPath = logFilePath(process, QStringLiteral(".log"));
    const QString codexPath = logFilePath(process, QStringLiteral("-codex.log"));

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (level != LogLevel::Debug) {
        writeLine(mainPath, line);
    } else if (g_codexTraceEnabled) {
        writeLine(mainPath, line);
    }

    if (g_codexTraceEnabled) {
        writeLine(codexPath, line);
    }
}

} // namespace khronicle::logging
