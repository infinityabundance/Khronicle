import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Card {
    id: root

    property var snapshots: []
    property var sortedSnapshots: []

    signal compareRequested(var snapshotA, var snapshotB)

    function selectForTimestamp(iso) {
        if (!iso || !root.sortedSnapshots || root.sortedSnapshots.length === 0) {
            return
        }
        const target = new Date(iso)
        if (isNaN(target.getTime())) {
            return
        }

        var closestIndex = 0
        var closestDelta = Math.abs(new Date(root.sortedSnapshots[0].timestamp).getTime()
            - target.getTime())

        for (var i = 1; i < root.sortedSnapshots.length; ++i) {
            const snapTime = new Date(root.sortedSnapshots[i].timestamp)
            if (isNaN(snapTime.getTime())) {
                continue
            }
            const delta = Math.abs(snapTime.getTime() - target.getTime())
            if (delta < closestDelta) {
                closestDelta = delta
                closestIndex = i
            }
        }

        compareFrom.currentIndex = closestIndex
        compareTo.currentIndex = 0
        if (compareTo.currentIndex === compareFrom.currentIndex
            && root.sortedSnapshots.length > 1) {
            compareTo.currentIndex = 1
        }
    }

    function formatTimestamp(iso) {
        if (!iso) {
            return ""
        }
        const date = new Date(iso)
        if (isNaN(date.getTime())) {
            return iso
        }
        return date.toLocaleString()
    }

    function formatSnapshot(snapshot) {
        if (!snapshot) {
            return ""
        }
        const timestamp = formatTimestamp(snapshot.timestamp)
        const kernel = snapshot.kernelVersion || ""
        if (timestamp && kernel) {
            return timestamp + " • " + kernel
        }
        return timestamp || kernel || ""
    }

    function updateSortedSnapshots() {
        if (!root.snapshots || root.snapshots.length === 0) {
            root.sortedSnapshots = []
            return
        }
        const sorted = root.snapshots.slice().sort(function(a, b) {
            const aTime = a.timestamp || ""
            const bTime = b.timestamp || ""
            return bTime.localeCompare(aTime)
        })
        root.sortedSnapshots = sorted.map(function(snapshot) {
            return {
                id: snapshot.id,
                timestamp: snapshot.timestamp,
                kernelVersion: snapshot.kernelVersion,
                label: formatSnapshot(snapshot)
            }
        })

        if (sorted.length >= 2) {
            compareFrom.currentIndex = 1
            compareTo.currentIndex = 0
        } else if (sorted.length === 1) {
            compareFrom.currentIndex = 0
            compareTo.currentIndex = 0
        }
    }

    onSnapshotsChanged: updateSortedSnapshots()

    contentItem: ColumnLayout {
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            level: 3
            text: "Compare snapshots"
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Kirigami.Units.largeSpacing

            ColumnLayout {
                Layout.fillWidth: true

                Label {
                    text: "Compare from"
                    opacity: 0.7
                }

                ComboBox {
                    id: compareFrom
                    Layout.fillWidth: true
                    model: root.sortedSnapshots
                    textRole: "label"
                    valueRole: "id"

                    delegate: ItemDelegate {
                        width: compareFrom.width
                        text: model.label || ""
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true

                Label {
                    text: "Compare to"
                    opacity: 0.7
                }

                ComboBox {
                    id: compareTo
                    Layout.fillWidth: true
                    model: root.sortedSnapshots
                    textRole: "label"
                    valueRole: "id"

                    delegate: ItemDelegate {
                        width: compareTo.width
                        text: model.label || ""
                    }
                }
            }

            Button {
                text: "Compare"
                enabled: root.sortedSnapshots.length > 0
                Layout.alignment: Qt.AlignBottom
                onClicked: {
                    if (compareFrom.currentIndex < 0 || compareTo.currentIndex < 0) {
                        return
                    }
                    const fromSnapshot = root.sortedSnapshots[compareFrom.currentIndex]
                    const toSnapshot = root.sortedSnapshots[compareTo.currentIndex]
                    if (!fromSnapshot || !toSnapshot) {
                        return
                    }
                    root.compareRequested(fromSnapshot, toSnapshot)
                }
            }
        }
    }
}
