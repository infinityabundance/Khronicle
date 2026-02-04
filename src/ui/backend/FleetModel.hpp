#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QHash>

namespace khronicle {

class FleetModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList hosts READ hosts NOTIFY hostsChanged)
    Q_PROPERTY(QVariantList events READ events NOTIFY eventsChanged)
    Q_PROPERTY(QVariantList snapshots READ snapshots NOTIFY snapshotsChanged)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(QString selectedHostId READ selectedHostId NOTIFY selectedHostChanged)

public:
    explicit FleetModel(QObject *parent = nullptr);

    Q_INVOKABLE void loadAggregateFile(const QString &path);
    Q_INVOKABLE void setSelectedHostId(const QString &hostId);
    Q_INVOKABLE QVariantList compareHostsLast24h(const QString &hostIdA,
                                                 const QString &hostIdB) const;

    QVariantList hosts() const;
    QVariantList events() const;
    QVariantList snapshots() const;
    QVariantMap summary() const;
    QString selectedHostId() const;

signals:
    void hostsChanged();
    void eventsChanged();
    void snapshotsChanged();
    void summaryChanged();
    void selectedHostChanged();
    void errorOccurred(const QString &message);

private:
    void updateSelectedHostData();
    QVariantMap buildSummary(const QVariantList &events) const;

    QVariantList m_hosts;
    QHash<QString, QVariantList> m_eventsByHost;
    QHash<QString, QVariantList> m_snapshotsByHost;
    QString m_selectedHostId;
    QVariantList m_currentEvents;
    QVariantList m_currentSnapshots;
    QVariantMap m_currentSummary;
};

} // namespace khronicle
