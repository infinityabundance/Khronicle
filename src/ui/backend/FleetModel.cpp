#include "ui/backend/FleetModel.hpp"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace khronicle {

namespace {

QVariantList jsonArrayToVariantList(const QJsonValue &value)
{
    QVariantList list;
    if (!value.isArray()) {
        return list;
    }
    const QJsonArray array = value.toArray();
    list.reserve(array.size());
    for (const QJsonValue &item : array) {
        list.push_back(item.toObject().toVariantMap());
    }
    return list;
}

QString displayNameForHost(const QVariantMap &host)
{
    const QString displayName = host.value("displayName").toString();
    if (!displayName.isEmpty()) {
        return displayName;
    }
    const QString hostname = host.value("hostname").toString();
    if (!hostname.isEmpty()) {
        return hostname;
    }
    return host.value("hostId").toString();
}

} // namespace

FleetModel::FleetModel(QObject *parent)
    : QObject(parent)
{
}

void FleetModel::loadAggregateFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(QStringLiteral("Failed to open aggregate file"));
        return;
    }

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        emit errorOccurred(QStringLiteral("Invalid aggregate JSON"));
        return;
    }

    const QJsonObject root = doc.object();
    const QJsonArray hostsArray = root.value("hosts").toArray();

    m_hosts.clear();
    m_eventsByHost.clear();
    m_snapshotsByHost.clear();

    for (const QJsonValue &hostValue : hostsArray) {
        if (!hostValue.isObject()) {
            continue;
        }
        const QJsonObject hostObj = hostValue.toObject();
        const QJsonObject identity = hostObj.value("hostIdentity").toObject();

        QVariantMap hostMap = identity.toVariantMap();
        const QString hostId = hostMap.value("hostId").toString();
        if (hostId.isEmpty()) {
            continue;
        }

        hostMap["label"] = displayNameForHost(hostMap);
        m_hosts.push_back(hostMap);

        m_eventsByHost.insert(hostId, jsonArrayToVariantList(hostObj.value("events")));
        m_snapshotsByHost.insert(hostId, jsonArrayToVariantList(hostObj.value("snapshots")));
    }

    emit hostsChanged();

    if (!m_hosts.isEmpty()) {
        setSelectedHostId(m_hosts.first().toMap().value("hostId").toString());
    } else {
        m_selectedHostId.clear();
        m_currentEvents.clear();
        m_currentSnapshots.clear();
        m_currentSummary.clear();
        emit selectedHostChanged();
        emit eventsChanged();
        emit snapshotsChanged();
        emit summaryChanged();
    }
}

void FleetModel::setSelectedHostId(const QString &hostId)
{
    if (hostId == m_selectedHostId) {
        return;
    }
    m_selectedHostId = hostId;
    updateSelectedHostData();
    emit selectedHostChanged();
}

QVariantList FleetModel::compareHostsLast24h(const QString &hostIdA,
                                             const QString &hostIdB) const
{
    QVariantList buckets;
    if (!m_eventsByHost.contains(hostIdA) || !m_eventsByHost.contains(hostIdB)) {
        return buckets;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime cutoff = now.addSecs(-24 * 3600);

    for (int hour = 0; hour < 24; ++hour) {
        const QDateTime bucketStart = cutoff.addSecs(hour * 3600);
        const QDateTime bucketEnd = bucketStart.addSecs(3600);

        QVariantList hostAEvents;
        QVariantList hostBEvents;

        const auto eventsA = m_eventsByHost.value(hostIdA);
        const auto eventsB = m_eventsByHost.value(hostIdB);

        for (const QVariant &eventVar : eventsA) {
            const QVariantMap event = eventVar.toMap();
            const QDateTime ts = QDateTime::fromString(event.value("timestamp").toString(), Qt::ISODate);
            if (ts >= bucketStart && ts < bucketEnd) {
                hostAEvents.push_back(eventVar);
            }
        }
        for (const QVariant &eventVar : eventsB) {
            const QVariantMap event = eventVar.toMap();
            const QDateTime ts = QDateTime::fromString(event.value("timestamp").toString(), Qt::ISODate);
            if (ts >= bucketStart && ts < bucketEnd) {
                hostBEvents.push_back(eventVar);
            }
        }

        QVariantMap bucket;
        bucket["timeBucket"] = bucketStart.toString(Qt::ISODate);
        bucket["hostAEvents"] = hostAEvents;
        bucket["hostBEvents"] = hostBEvents;
        buckets.push_back(bucket);
    }

    return buckets;
}

QVariantList FleetModel::hosts() const
{
    return m_hosts;
}

QVariantList FleetModel::events() const
{
    return m_currentEvents;
}

QVariantList FleetModel::snapshots() const
{
    return m_currentSnapshots;
}

QVariantMap FleetModel::summary() const
{
    return m_currentSummary;
}

QString FleetModel::selectedHostId() const
{
    return m_selectedHostId;
}

void FleetModel::updateSelectedHostData()
{
    m_currentEvents = m_eventsByHost.value(m_selectedHostId);
    m_currentSnapshots = m_snapshotsByHost.value(m_selectedHostId);
    m_currentSummary = buildSummary(m_currentEvents);

    emit eventsChanged();
    emit snapshotsChanged();
    emit summaryChanged();
}

QVariantMap FleetModel::buildSummary(const QVariantList &events) const
{
    QVariantMap summary;
    if (events.isEmpty()) {
        return summary;
    }

    int gpuEvents = 0;
    int firmwareEvents = 0;
    bool kernelChanged = false;
    QString kernelFrom;
    QString kernelTo;

    for (const QVariant &eventVar : events) {
        const QVariantMap event = eventVar.toMap();
        const QString category = event.value("category").toString();
        if (category == "kernel") {
            kernelChanged = true;
            const QVariantMap beforeState = event.value("beforeState").toMap();
            const QVariantMap afterState = event.value("afterState").toMap();
            if (kernelFrom.isEmpty()) {
                kernelFrom = beforeState.value("kernelVersion").toString();
            }
            if (!afterState.value("kernelVersion").toString().isEmpty()) {
                kernelTo = afterState.value("kernelVersion").toString();
            }
        } else if (category == "gpu_driver") {
            gpuEvents++;
        } else if (category == "firmware") {
            firmwareEvents++;
        }
    }

    summary["kernelChanged"] = kernelChanged;
    summary["kernelFrom"] = kernelFrom;
    summary["kernelTo"] = kernelTo;
    summary["gpuEvents"] = gpuEvents;
    summary["firmwareEvents"] = firmwareEvents;
    summary["totalEvents"] = events.size();
    return summary;
}

} // namespace khronicle
