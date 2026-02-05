#include "tray/KhronicleTray.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QIcon>
#include <QMessageBox>
#include <QTime>
#include <QStringList>

#include <unistd.h>

#include <nlohmann/json.hpp>

#include "common/logging.hpp"
#include "common/process_utils.hpp"

namespace {

constexpr int kRefreshIntervalMs = 15 * 60 * 1000;
constexpr int kSocketTimeoutMs = 1500;

QString toIso8601Utc(const QDateTime &dt)
{
    return dt.toUTC().toString(Qt::ISODate);
}

} // namespace

KhronicleTray::KhronicleTray(QObject *parent)
    : QObject(parent)
{
    // Tray is intentionally lightweight: no DB access, only local JSON-RPC.
    KLOG_INFO(QStringLiteral("KhronicleTray"),
              QStringLiteral("KhronicleTray"),
              QStringLiteral("tray_start"),
              QStringLiteral("user_start"),
              QStringLiteral("tray"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());
    setupTrayIcon();
    setupMenu();
    scheduleRefresh();
}

KhronicleTray::~KhronicleTray() = default;

void KhronicleTray::setupTrayIcon()
{
    m_trayIcon.setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    m_trayIcon.setToolTip(QStringLiteral("Khronicle - System change summary"));

    connect(&m_trayIcon, &QSystemTrayIcon::activated,
            this, &KhronicleTray::onTrayActivated);

    m_trayIcon.show();
}

void KhronicleTray::setupMenu()
{
    m_daemonStatusAction = m_menu.addAction(QStringLiteral("Daemon: Unknown"));
    m_daemonStatusAction->setEnabled(false);

    m_startStopDaemonAction = m_menu.addAction(QStringLiteral("Start daemon"));
    connect(m_startStopDaemonAction, &QAction::triggered, this, [this]() {
        if (khronicle::isDaemonRunning()) {
            khronicle::stopDaemon();
        } else {
            khronicle::startDaemon();
        }
        updateDaemonActions();
    });

    m_menu.addSeparator();

    auto *showAction = m_menu.addAction(QStringLiteral("Show Today's Changes"));
    connect(showAction, &QAction::triggered, this, &KhronicleTray::showSummaryPopup);

    m_watchSignalsAction = m_menu.addAction(QStringLiteral("Show Watchpoint Signals"));
    connect(m_watchSignalsAction, &QAction::triggered,
            this, &KhronicleTray::showWatchSignalsPopup);

    m_menu.addSeparator();

    m_openAppAction = m_menu.addAction(QStringLiteral("Open Khronicle UI"));
    connect(m_openAppAction, &QAction::triggered, this, &KhronicleTray::openFullApp);

    m_menu.addSeparator();

    m_aboutAction = m_menu.addAction(QStringLiteral("About Khronicle"));
    connect(m_aboutAction, &QAction::triggered, this, &KhronicleTray::showAboutDialog);

    m_refreshAction = m_menu.addAction(QStringLiteral("Refresh Now"));
    connect(m_refreshAction, &QAction::triggered, this, &KhronicleTray::refreshSummary);

    m_quitAction = m_menu.addAction(QStringLiteral("Quit"));
    connect(m_quitAction, &QAction::triggered, qApp, &QCoreApplication::quit);

    m_trayIcon.setContextMenu(&m_menu);
}

void KhronicleTray::scheduleRefresh()
{
    m_refreshTimer.setInterval(kRefreshIntervalMs);
    connect(&m_refreshTimer, &QTimer::timeout, this, &KhronicleTray::refreshSummary);
    m_refreshTimer.start();

    refreshSummary();
    updateDaemonActions();
}

void KhronicleTray::refreshSummary()
{
    // Periodic refresh of today's summary and critical watchpoints for tooltip.
    KLOG_DEBUG(QStringLiteral("KhronicleTray"),
               QStringLiteral("refreshSummary"),
               QStringLiteral("fetch_today_summary"),
               QStringLiteral("timer_tick"),
               QStringLiteral("json_rpc"),
               khronicle::logging::defaultWho(),
               QString(),
               nlohmann::json::object());
    m_lastSummaryText = requestSummarySinceToday();
    const int criticalSignals = requestCriticalWatchSignalsSinceToday();
    if (criticalSignals > 0) {
        m_lastSummaryText += QStringLiteral(" (%1 critical watchpoint hit)")
            .arg(criticalSignals);
    }
    m_trayIcon.setToolTip(QStringLiteral("Khronicle - ") + m_lastSummaryText);
    updateDaemonActions();
}

void KhronicleTray::showSummaryPopup()
{
    // On-demand popup for today's summary.
    if (m_lastSummaryText.isEmpty()) {
        refreshSummary();
    }

    KLOG_INFO(QStringLiteral("KhronicleTray"),
              QStringLiteral("showSummaryPopup"),
              QStringLiteral("show_summary_popup"),
              QStringLiteral("user_action"),
              QStringLiteral("tray_popup"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());
    m_trayIcon.showMessage(QStringLiteral("Khronicle - Today's Changes"),
                           m_lastSummaryText,
                           QSystemTrayIcon::Information);
}

void KhronicleTray::showWatchSignalsPopup()
{
    // Simple textual list of the most recent watchpoint signals.
    KLOG_INFO(QStringLiteral("KhronicleTray"),
              QStringLiteral("showWatchSignalsPopup"),
              QStringLiteral("show_watch_signals_popup"),
              QStringLiteral("user_action"),
              QStringLiteral("tray_popup"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());
    const QString summary = requestWatchSignalsSinceToday();
    m_trayIcon.showMessage(QStringLiteral("Khronicle - Watchpoint Signals"),
                           summary,
                           QSystemTrayIcon::Information);
}

void KhronicleTray::openFullApp()
{
    KLOG_INFO(QStringLiteral("KhronicleTray"),
              QStringLiteral("openFullApp"),
              QStringLiteral("open_full_app"),
              QStringLiteral("user_action"),
              QStringLiteral("process_start"),
              khronicle::logging::defaultWho(),
              QString(),
              nlohmann::json::object());
    khronicle::startUi();
}

void KhronicleTray::showAboutDialog()
{
    QMessageBox box;
    box.setWindowTitle(QStringLiteral("About Khronicle"));
    box.setTextFormat(Qt::RichText);
    box.setTextInteractionFlags(Qt::TextBrowserInteraction);
    box.setStandardButtons(QMessageBox::Ok);
    box.setText(QStringLiteral("<b>Khronicle</b><br/>"
                               "System change chronicle for CachyOS/Arch-like systems.<br/>"
                               "<a href=\"https://github.com/infinityabundance/Khronicle\">"
                               "https://github.com/infinityabundance/Khronicle</a>"));
    box.exec();
}

void KhronicleTray::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger) {
        KLOG_INFO(QStringLiteral("KhronicleTray"),
                  QStringLiteral("onTrayActivated"),
                  QStringLiteral("open_full_app"),
                  QStringLiteral("tray_click"),
                  QStringLiteral("process_start"),
                  khronicle::logging::defaultWho(),
                  QString(),
                  nlohmann::json::object());
        openFullApp();
    } else if (reason == QSystemTrayIcon::DoubleClick) {
        showSummaryPopup();
    }
}

QString KhronicleTray::requestSummarySinceToday()
{
    // Query the daemon for summary_since starting at local midnight.
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(kSocketTimeoutMs)) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime midnight = QDateTime(now.date(), QTime(0, 0));

    QJsonObject params;
    params["since"] = toIso8601Utc(midnight);

    QJsonObject root;
    root["id"] = 1;
    root["method"] = QStringLiteral("summary_since");
    root["params"] = params;

    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    socket.write(payload);
    if (!socket.waitForBytesWritten(kSocketTimeoutMs)) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    if (!socket.waitForReadyRead(kSocketTimeoutMs)) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    const QByteArray responseLine = socket.readLine();
    if (responseLine.isEmpty()) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    const QJsonObject result = obj.value("result").toObject();
    if (result.isEmpty()) {
        return QStringLiteral("No summary available (daemon not running?)");
    }

    const bool kernelChanged = result.value("kernelChanged").toBool(false);
    const QString kernelFrom = result.value("kernelFrom").toString();
    const QString kernelTo = result.value("kernelTo").toString();
    const int gpuEvents = result.value("gpuEvents").toInt(0);
    const int firmwareEvents = result.value("firmwareEvents").toInt(0);
    const int totalEvents = result.value("totalEvents").toInt(0);

    QString summary;
    if (kernelChanged) {
        summary += QStringLiteral("Kernel: ")
            + (kernelFrom.isEmpty() ? QStringLiteral("?") : kernelFrom)
            + QStringLiteral(" -> ")
            + (kernelTo.isEmpty() ? QStringLiteral("?") : kernelTo);
    } else {
        summary += QStringLiteral("No kernel change");
    }

    summary += QStringLiteral("; GPU events: ") + QString::number(gpuEvents);
    summary += QStringLiteral("; Firmware: ") + QString::number(firmwareEvents);
    summary += QStringLiteral("; Total: ") + QString::number(totalEvents);

    return summary;
}

int KhronicleTray::requestCriticalWatchSignalsSinceToday()
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(kSocketTimeoutMs)) {
        return 0;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime midnight = QDateTime(now.date(), QTime(0, 0));

    QJsonObject params;
    params["since"] = toIso8601Utc(midnight);

    QJsonObject root;
    root["id"] = 1;
    root["method"] = QStringLiteral("get_watch_signals_since");
    root["params"] = params;

    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    socket.write(payload);
    if (!socket.waitForBytesWritten(kSocketTimeoutMs)) {
        return 0;
    }

    if (!socket.waitForReadyRead(kSocketTimeoutMs)) {
        return 0;
    }

    const QByteArray responseLine = socket.readLine();
    if (responseLine.isEmpty()) {
        return 0;
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return 0;
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        return 0;
    }

    const QJsonObject result = obj.value("result").toObject();
    const QJsonArray watchSignals = result.value("signals").toArray();
    int count = 0;
    for (const auto &value : watchSignals) {
        const QJsonObject signal = value.toObject();
        if (signal.value("severity").toString() == QStringLiteral("critical")) {
            count++;
        }
    }
    return count;
}

QString KhronicleTray::requestWatchSignalsSinceToday()
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(kSocketTimeoutMs)) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    const QDateTime now = QDateTime::currentDateTime();
    const QDateTime midnight = QDateTime(now.date(), QTime(0, 0));

    QJsonObject params;
    params["since"] = toIso8601Utc(midnight);

    QJsonObject root;
    root["id"] = 1;
    root["method"] = QStringLiteral("get_watch_signals_since");
    root["params"] = params;

    const QByteArray payload =
        QJsonDocument(root).toJson(QJsonDocument::Compact) + '\n';

    socket.write(payload);
    if (!socket.waitForBytesWritten(kSocketTimeoutMs)) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    if (!socket.waitForReadyRead(kSocketTimeoutMs)) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    const QByteArray responseLine = socket.readLine();
    if (responseLine.isEmpty()) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(responseLine, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    const QJsonObject obj = doc.object();
    if (obj.contains("error")) {
        return QStringLiteral("No watchpoint signals (daemon not running?)");
    }

    const QJsonObject result = obj.value("result").toObject();
    const QJsonArray watchSignals = result.value("signals").toArray();
    if (watchSignals.isEmpty()) {
        return QStringLiteral("No watchpoint signals today");
    }

    QStringList lines;
    const int maxSignals = 5;
    for (int i = watchSignals.size() - 1; i >= 0 && lines.size() < maxSignals; --i) {
        const QJsonObject signal = watchSignals.at(i).toObject();
        const QString timestamp = signal.value("timestamp").toString();
        const QDateTime when = QDateTime::fromString(timestamp, Qt::ISODate);
        const QString timeLabel = when.isValid()
            ? when.toLocalTime().toString("HH:mm")
            : QStringLiteral("??:??");
        const QString ruleName = signal.value("ruleName").toString();
        const QString severity = signal.value("severity").toString();
        const QString message = signal.value("message").toString();

        lines << QStringLiteral("%1 [%2] %3 - %4")
            .arg(timeLabel, severity, ruleName, message);
    }

    return lines.join('\n');
}

QString KhronicleTray::socketPath() const
{
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        return runtimeDir + QStringLiteral("/khronicle.sock");
    }
    return QStringLiteral("/run/user/%1/khronicle.sock").arg(getuid());
}

void KhronicleTray::updateDaemonActions()
{
    const bool running = khronicle::isDaemonRunning();
    if (m_daemonStatusAction) {
        m_daemonStatusAction->setText(
            running ? QStringLiteral("Daemon: Running")
                    : QStringLiteral("Daemon: Stopped"));
    }
    if (m_startStopDaemonAction) {
        m_startStopDaemonAction->setText(
            running ? QStringLiteral("Stop daemon")
                    : QStringLiteral("Start daemon"));
    }
}
