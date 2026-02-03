import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    property var summary: ({})

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
                text: "Since yesterday"
            }

            Kirigami.Label {
                visible: Object.keys(root.summary).length === 0
                text: "No changes recorded yet."
                opacity: 0.6
            }

            ColumnLayout {
                visible: Object.keys(root.summary).length !== 0
                spacing: Kirigami.Units.smallSpacing

                Kirigami.Label {
                    text: root.summary.kernelChanged ?
                          ("Kernel: " + (root.summary.kernelFrom || "?") + " → " + (root.summary.kernelTo || "?")) :
                          "Kernel: no change"
                }

                Kirigami.Label {
                    text: "GPU events: " + (root.summary.gpuEvents || 0)
                }

                Kirigami.Label {
                    text: "Firmware updates: " + (root.summary.firmwareEvents || 0)
                }

                Kirigami.Label {
                    text: "Total changes: " + (root.summary.totalEvents || 0)
                }
            }
        }
    }
}
