import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Kirigami.Card {
    id: root
    property var diffRows: []

    function humanizePath(path) {
        if (!path) {
            return ""
        }
        if (path === "kernelVersion") {
            return "Kernel"
        }
        if (path.indexOf("keyPackages.") === 0) {
            return path.substring("keyPackages.".length)
        }
        if (path.indexOf("firmwareVersions.") === 0) {
            return "Firmware: " + path.substring("firmwareVersions.".length)
        }
        return path
    }

    contentItem: ColumnLayout {
        Layout.fillWidth: true
        anchors.margins: Kirigami.Units.largeSpacing
        spacing: Kirigami.Units.smallSpacing

        Kirigami.Heading {
            level: 3
            text: "Snapshot diff"
        }

        Kirigami.PlaceholderMessage {
            visible: !(root.diffRows && root.diffRows.length > 0)
            text: "No differences between these snapshots."
        }

        ColumnLayout {
            visible: root.diffRows && root.diffRows.length > 0
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Layout.fillWidth: true

                Label {
                    text: "Change"
                    opacity: 0.7
                    Layout.preferredWidth: root.width * 0.3
                }

                Label {
                    text: "Before"
                    opacity: 0.7
                    Layout.preferredWidth: root.width * 0.35
                }

                Label {
                    text: "After"
                    opacity: 0.7
                    Layout.preferredWidth: root.width * 0.35
                }
            }

            Repeater {
                model: root.diffRows

                delegate: ItemDelegate {
                    width: root.width

                    contentItem: RowLayout {
                        spacing: Kirigami.Units.largeSpacing
                        anchors.margins: Kirigami.Units.smallSpacing
                        Layout.fillWidth: true

                        Label {
                            text: humanizePath(modelData.path)
                            font.bold: true
                            Layout.preferredWidth: root.width * 0.3
                            wrapMode: Text.Wrap
                        }

                        Label {
                            text: modelData.before || ""
                            Layout.preferredWidth: root.width * 0.35
                            wrapMode: Text.Wrap
                        }

                        Label {
                            text: modelData.after || ""
                            Layout.preferredWidth: root.width * 0.35
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
