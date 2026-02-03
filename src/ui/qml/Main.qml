import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "Khronicle"
    width: 1000
    height: 700
    visible: true

    property var eventsModel: []
    property var summaryData: ({})
    property var snapshotsModel: []
    property var diffModel: []

    Component.onCompleted: {
        khronicleApi.connectToDaemon()

        const now = new Date()
        const yesterday = new Date(now.getTime() - 24 * 60 * 60 * 1000)
        const weekAgo = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)

        khronicleApi.loadSummarySince(yesterday)
        khronicleApi.loadChangesBetween(weekAgo, now)
        khronicleApi.loadSnapshots()
    }

    Connections {
        target: khronicleApi
        function onSummaryLoaded(summary) {
            root.summaryData = summary
        }
        function onChangesLoaded(events) {
            root.eventsModel = events
        }
        function onSnapshotsLoaded(snapshots) {
            root.snapshotsModel = snapshots
        }
        function onDiffLoaded(diffRows) {
            root.diffModel = diffRows
        }
        function onErrorOccurred(message) {
            console.warn("Khronicle API error:", message)
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: "Khronicle"

        ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.largeSpacing

            SummaryBar {
                Layout.fillWidth: true
                summary: root.summaryData
            }

            TimelineView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                events: root.eventsModel
            }

            SnapshotSelector {
                Layout.fillWidth: true
                snapshots: root.snapshotsModel
                onCompareRequested: function(snapshotAId, snapshotBId) {
                    khronicleApi.loadDiff(snapshotAId, snapshotBId)
                }
            }

            DiffView {
                Layout.fillWidth: true
                diffRows: root.diffModel
                visible: root.diffModel && root.diffModel.length > 0
            }
        }
    }
}
