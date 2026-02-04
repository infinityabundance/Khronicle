import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    property var summary: ({})
    property string rangeLabel: ""

    implicitHeight: contentCard.implicitHeight
    implicitWidth: contentCard.implicitWidth

    Kirigami.Card {
        id: contentCard
        anchors.fill: parent

        contentItem: ColumnLayout {
            anchors.margins: Kirigami.Units.largeSpacing
            spacing: Kirigami.Units.smallSpacing

            Kirigami.Heading {
                level: 3
                text: "Summary"
            }

            Label {
                visible: root.rangeLabel && root.rangeLabel.length > 0
                text: "Range: " + root.rangeLabel
                opacity: 0.7
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 0.9
            }

            Label {
                visible: Object.keys(root.summary).length === 0
                text: "No changes recorded yet."
                opacity: 0.6
            }

            ColumnLayout {
                visible: Object.keys(root.summary).length !== 0
                spacing: Kirigami.Units.smallSpacing

                Label {
                    text: root.summary.kernelChanged ?
                          ("Kernel: " + (root.summary.kernelFrom || "?") + " → " + (root.summary.kernelTo || "?")) :
                          "Kernel: no change"
                }

                Label {
                    text: "GPU events: " + (root.summary.gpuEvents || 0)
                }

                Label {
                    text: "Firmware updates: " + (root.summary.firmwareEvents || 0)
                }

                Label {
                    text: "Total changes: " + (root.summary.totalEvents || 0)
                }
            }
        }
    }
}
