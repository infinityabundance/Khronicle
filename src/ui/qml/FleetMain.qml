import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "Khronicle Fleet"
    width: 1100
    height: 700
    visible: true

    property var comparisonRows: []
    property string compareHostA: ""
    property string compareHostB: ""

    Connections {
        target: fleetModel
        function onErrorOccurred(message) {
            console.warn("Fleet mode error:", message)
        }
    }

    pageStack.initialPage: Kirigami.Page {
        title: "Fleet Mode"

        RowLayout {
            anchors.fill: parent
            spacing: Kirigami.Units.largeSpacing

            Kirigami.Card {
                Layout.preferredWidth: 240
                Layout.fillHeight: true

                contentItem: ColumnLayout {
                    anchors.margins: Kirigami.Units.largeSpacing
                    spacing: Kirigami.Units.smallSpacing

                    Kirigami.Heading {
                        level: 3
                        text: "Hosts"
                    }

                    ListView {
                        id: hostList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        model: fleetModel.hosts

                        delegate: ItemDelegate {
                            width: hostList.width
                            text: modelData.label || modelData.hostname || modelData.hostId
                            onClicked: fleetModel.setSelectedHostId(modelData.hostId)
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: Kirigami.Units.largeSpacing

                SummaryBar {
                    Layout.fillWidth: true
                    summary: fleetModel.summary
                }

                TimelineView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    events: fleetModel.events
                }

                Kirigami.Card {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        anchors.margins: Kirigami.Units.largeSpacing
                        spacing: Kirigami.Units.smallSpacing

                        Kirigami.Heading {
                            level: 3
                            text: "Compare Hosts (Last 24h)"
                        }

                        RowLayout {
                            spacing: Kirigami.Units.smallSpacing
                            Layout.fillWidth: true

                            ComboBox {
                                id: hostASelect
                                Layout.fillWidth: true
                                model: fleetModel.hosts
                                textRole: "label"
                                onActivated: {
                                    compareHostA = fleetModel.hosts[hostASelect.currentIndex].hostId
                                }
                            }

                            ComboBox {
                                id: hostBSelect
                                Layout.fillWidth: true
                                model: fleetModel.hosts
                                textRole: "label"
                                onActivated: {
                                    compareHostB = fleetModel.hosts[hostBSelect.currentIndex].hostId
                                }
                            }

                            Button {
                                text: "Compare"
                                onClicked: {
                                    comparisonRows = fleetModel.compareHostsLast24h(compareHostA, compareHostB)
                                }
                            }
                        }

                        ListView {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 150
                            model: comparisonRows
                            clip: true

                            delegate: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                width: parent.width

                                Label {
                                    text: modelData.timeBucket
                                    Layout.preferredWidth: 180
                                }

                                Label {
                                    text: "Host A: " + modelData.hostAEvents.length
                                }

                                Label {
                                    text: "Host B: " + modelData.hostBEvents.length
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
