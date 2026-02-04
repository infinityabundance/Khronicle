import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "Khronicle"
    width: 1000
    height: 700
    visible: true

    property bool filterKernel: true
    property bool filterGpu: true
    property bool filterFirmware: true
    property bool filterPackage: true

    property var currentFromDate: null
    property var currentToDate: null
    property string currentRange: "week"

    property var rawEventsModel: []
    property var eventsModel: []
    property var summaryData: ({})
    property var snapshotsModel: []
    property var diffModel: []
    property string explanationText: ""

    function applyFilters() {
        if (!root.rawEventsModel) {
            root.eventsModel = []
            return
        }

        var filtered = []
        for (var i = 0; i < root.rawEventsModel.length; ++i) {
            var ev = root.rawEventsModel[i]
            var cat = (ev.category || "").toString().toLowerCase()

            var keep = false
            if (cat === "kernel" && root.filterKernel) {
                keep = true
            } else if ((cat === "gpu_driver" || cat === "gpu") && root.filterGpu) {
                keep = true
            } else if (cat === "firmware" && root.filterFirmware) {
                keep = true
            } else if (cat === "package" && root.filterPackage) {
                keep = true
            } else if (cat === "system") {
                keep = true
            }

            if (keep) {
                filtered.push(ev)
            }
        }

        filtered.sort(function(a, b) {
            const aTime = a.timestamp || ""
            const bTime = b.timestamp || ""
            return bTime.localeCompare(aTime)
        })

        root.eventsModel = filtered
    }

    function selectDateRange(kind) {
        const now = new Date()
        var from = null
        var to = now

        root.currentRange = kind

        if (kind === "today") {
            from = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0)
        } else if (kind === "yesterday") {
            const yesterdayDate = new Date(
                now.getFullYear(),
                now.getMonth(),
                now.getDate() - 1,
                0,
                0,
                0
            )
            from = yesterdayDate
            to = new Date(now.getFullYear(), now.getMonth(), now.getDate(), 0, 0, 0)
        } else if (kind === "week") {
            from = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)
        } else {
            from = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)
        }

        root.currentFromDate = from
        root.currentToDate = to

        khronicleApi.loadChangesBetween(from, to)
        khronicleApi.loadSummarySince(from)
    }

    function buildRangeLabel() {
        if (!root.currentFromDate || !root.currentToDate) {
            return ""
        }

        function formatDate(d) {
            if (!d) {
                return ""
            }
            return d.toLocaleDateString(Qt.locale(), "yyyy-MM-dd")
        }

        return formatDate(root.currentFromDate) + " → " + formatDate(root.currentToDate)
    }

    Component.onCompleted: {
        khronicleApi.connectToDaemon()
        selectDateRange("week")
        khronicleApi.loadSnapshots()
    }

    Connections {
        target: khronicleApi
        function onSummaryLoaded(summary) {
            root.summaryData = summary
        }
        function onChangesLoaded(events) {
            root.rawEventsModel = events
            root.applyFilters()
        }
        function onSnapshotsLoaded(snapshots) {
            root.snapshotsModel = snapshots
        }
        function onDiffLoaded(diffRows) {
            root.diffModel = diffRows
        }
        function onExplanationLoaded(summary) {
            root.explanationText = summary
        }
        function onErrorOccurred(message) {
            console.warn("Khronicle API error:", message)
        }
    }

    Timer {
        interval: 5000
        repeat: true
        running: true
        onTriggered: {
            if (daemonController) {
                daemonController.refreshDaemonStatus()
            }
        }
    }

    globalDrawer: Kirigami.GlobalDrawer {
        title: "Khronicle"
        actions: [
            Kirigami.Action {
                text: "Overview"
                onTriggered: root.pageStack.replace(overviewPageComponent)
            },
            Kirigami.Action {
                text: "Watchpoints"
                onTriggered: root.pageStack.replace(watchpointsPageComponent)
            },
            Kirigami.Action {
                text: "About"
                onTriggered: aboutDialog.open()
            }
        ]
    }

    Component {
        id: overviewPageComponent
        Kirigami.Page {
        title: "Khronicle"

        ColumnLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.largeSpacing

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: daemonController && daemonController.daemonRunning
                        ? "Daemon: Running"
                        : "Daemon: Stopped"
                }

                Button {
                    text: daemonController && daemonController.daemonRunning
                        ? "Stop daemon"
                        : "Start daemon"
                    onClicked: {
                        if (!daemonController) {
                            return
                        }
                        if (daemonController.daemonRunning) {
                            daemonController.stopDaemonFromUi()
                        } else {
                            daemonController.startDaemonFromUi()
                        }
                    }
                }

                Button {
                    text: "Start tray"
                    onClicked: {
                        if (daemonController) {
                            daemonController.startTrayFromUi()
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }

            SummaryBar {
                Layout.fillWidth: true
                summary: root.summaryData
                rangeLabel: root.buildRangeLabel()
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.largeSpacing

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    CheckBox {
                        text: "Kernel"
                        checked: root.filterKernel
                        onToggled: {
                            root.filterKernel = checked
                            root.applyFilters()
                        }
                    }

                    CheckBox {
                        text: "GPU"
                        checked: root.filterGpu
                        onToggled: {
                            root.filterGpu = checked
                            root.applyFilters()
                        }
                    }

                    CheckBox {
                        text: "Firmware"
                        checked: root.filterFirmware
                        onToggled: {
                            root.filterFirmware = checked
                            root.applyFilters()
                        }
                    }

                    CheckBox {
                        text: "Packages"
                        checked: root.filterPackage
                        onToggled: {
                            root.filterPackage = checked
                            root.applyFilters()
                        }
                    }
                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Chip {
                        text: "Today"
                        checkable: true
                        checked: root.currentRange === "today"
                        onClicked: {
                            selectDateRange("today")
                        }
                    }

                    Kirigami.Chip {
                        text: "Yesterday"
                        checkable: true
                        checked: root.currentRange === "yesterday"
                        onClicked: {
                            selectDateRange("yesterday")
                        }
                    }

                    Kirigami.Chip {
                        text: "Last 7 days"
                        checkable: true
                        checked: root.currentRange === "week"
                        onClicked: {
                            selectDateRange("week")
                        }
                    }

                    Kirigami.Chip {
                        text: "Custom"
                        checkable: false
                        onClicked: {
                            console.log("Custom range not implemented yet")
                        }
                    }
                }
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

            RowLayout {
                Layout.fillWidth: true
                spacing: Kirigami.Units.smallSpacing

                Button {
                    text: "Explain this change"
                    enabled: root.currentFromDate !== null && root.currentToDate !== null
                    onClicked: {
                        root.explanationText = ""
                        khronicleApi.loadExplanationBetween(root.currentFromDate,
                                                           root.currentToDate)
                    }
                }

                Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: root.explanationText
                    visible: root.explanationText.length > 0
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

    Component {
        id: watchpointsPageComponent
        WatchpointsPage { }
    }

    Kirigami.Dialog {
        id: aboutDialog
        title: "About Khronicle"
        standardButtons: Kirigami.Dialog.Ok
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Heading { text: "Khronicle" }
            Label {
                text: "System change chronicle for CachyOS/Arch-like systems."
                wrapMode: Text.Wrap
            }
            Kirigami.LinkButton {
                text: "https://github.com/infinityabundance/Khronicle"
                onClicked: Qt.openUrlExternally(text)
            }
        }
    }

    pageStack.initialPage: overviewPageComponent
}
