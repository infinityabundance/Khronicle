#include "tray/KhronicleTray.hpp"

#include <QAction>
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QIcon>
#include <QTime>

#include <unistd.h>

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
    auto *showAction = m_menu.addAction(QStringLiteral("Show Today's Changes"));
    connect(showAction, &QAction::triggered, this, &KhronicleTray::showSummaryPopup);

    m_menu.addSeparator();

    m_openAppAction = m_menu.addAction(QStringLiteral("Open Khronicle..."));
    connect(m_openAppAction, &QAction::triggered, this, &KhronicleTray::openFullApp);

    m_menu.addSeparator();

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
}

void KhronicleTray::refreshSummary()
{
    m_lastSummaryText = requestSummarySinceToday();
    m_trayIcon.setToolTip(QStringLiteral("Khronicle - ") + m_lastSummaryText);
}

void KhronicleTray::showSummaryPopup()
{
    if (m_lastSummaryText.isEmpty()) {
        refreshSummary();
    }

    m_trayIcon.showMessage(QStringLiteral("Khronicle - Today's Changes"),
                           m_lastSummaryText,
                           QSystemTrayIcon::Information);
}

void KhronicleTray::openFullApp()
{
    QProcess::startDetached(QStringLiteral("khronicle"));
}

void KhronicleTray::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger
        || reason == QSystemTrayIcon::DoubleClick) {
        showSummaryPopup();
    }
}

QString KhronicleTray::requestSummarySinceToday()
{
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

QString KhronicleTray::socketPath() const
{
    const QString runtimeDir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (!runtimeDir.isEmpty()) {
        return runtimeDir + QStringLiteral("/khronicle.sock");
    }
    return QStringLiteral("/run/user/%1/khronicle.sock").arg(getuid());
}
