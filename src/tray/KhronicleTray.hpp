#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>

// KhronicleTray provides a minimal tray UI for quick, local summaries.
class KhronicleTray : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleTray(QObject *parent = nullptr);
    ~KhronicleTray() override;

private slots:
    void refreshSummary();
    void showSummaryPopup();
    void showWatchSignalsPopup();
    void openFullApp();
    void showAboutDialog();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    QSystemTrayIcon m_trayIcon;
    QMenu m_menu;
    QAction *m_refreshAction = nullptr;
    QAction *m_openAppAction = nullptr;
    QAction *m_watchSignalsAction = nullptr;
    QAction *m_daemonStatusAction = nullptr;
    QAction *m_startStopDaemonAction = nullptr;
    QAction *m_aboutAction = nullptr;
    QAction *m_quitAction = nullptr;
    QTimer m_refreshTimer;

    QString m_lastSummaryText;

    void setupTrayIcon();
    void setupMenu();
    void scheduleRefresh();
    void updateDaemonActions();

    QString requestSummarySinceToday();
    int requestCriticalWatchSignalsSinceToday();
    QString requestWatchSignalsSinceToday();
    QString socketPath() const;
};
