#pragma once

#include <QObject>

namespace khronicle {

class DaemonController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool daemonRunning READ daemonRunning NOTIFY daemonRunningChanged)
public:
    explicit DaemonController(QObject *parent = nullptr);

    bool daemonRunning() const;

    Q_INVOKABLE void refreshDaemonStatus();
    Q_INVOKABLE bool startDaemonFromUi();
    Q_INVOKABLE bool stopDaemonFromUi();
    Q_INVOKABLE bool startTrayFromUi();

signals:
    void daemonRunningChanged();

private:
    bool m_daemonRunning = false;
};

} // namespace khronicle
