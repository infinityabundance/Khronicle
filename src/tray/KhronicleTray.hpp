#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QTimer>

class KhronicleTray : public QObject
{
    Q_OBJECT
public:
    explicit KhronicleTray(QObject *parent = nullptr);
    ~KhronicleTray() override;

private slots:
    void refreshSummary();
    void showSummaryPopup();
    void openFullApp();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    QSystemTrayIcon m_trayIcon;
    QMenu m_menu;
    QAction *m_refreshAction = nullptr;
    QAction *m_openAppAction = nullptr;
    QAction *m_quitAction = nullptr;
    QTimer m_refreshTimer;

    QString m_lastSummaryText;

    void setupTrayIcon();
    void setupMenu();
    void scheduleRefresh();

    QString requestSummarySinceToday();
    QString socketPath() const;
};
