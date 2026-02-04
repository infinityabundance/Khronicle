import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ItemDelegate {
    id: root
    property var eventData: ({})

    function timePart(iso) {
        if (!iso) {
            return ""
        }
        const parts = iso.toString().split("T")
        if (parts.length < 2) {
            return iso
        }
        const timePart = parts[1]
        const timeChunks = timePart.split(/[:Z+]/)
        if (timeChunks.length < 2) {
            return timePart
        }
        return timeChunks[0] + ":" + timeChunks[1]
    }

    function categoryLabel(category) {
        switch (category) {
        case "kernel":
            return "Kernel"
        case "gpu_driver":
            return "GPU"
        case "firmware":
            return "Firmware"
        case "package":
            return "Package"
        case "system":
            return "System"
        default:
            return category || ""
        }
    }

    contentItem: ColumnLayout {
        anchors.margins: Kirigami.Units.smallSpacing
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            spacing: Kirigami.Units.smallSpacing

            Label {
                text: timePart(root.eventData.timestamp)
                opacity: 0.7
                Layout.alignment: Qt.AlignTop
            }

            ColumnLayout {
                spacing: Kirigami.Units.smallSpacing
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing

                    Label {
                        text: root.eventData.summary || ""
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                    }

                    Label {
                        text: categoryLabel(root.eventData.category || "")
                        opacity: 0.7
                    }
                }

                Label {
                    visible: (root.eventData.details || "").length > 0
                    text: root.eventData.details || ""
                    wrapMode: Text.Wrap
                    opacity: 0.7
                    font.pointSize: Kirigami.Theme.defaultFont.pointSize * 0.9
                }
            }
        }
    }
}
