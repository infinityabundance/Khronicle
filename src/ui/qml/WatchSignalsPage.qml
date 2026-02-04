import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: "Signals"

    property var signalsModel: []

    function loadRecent() {
        const now = new Date()
        const since = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000)
        watchClient.loadSignalsSince(since)
    }

    Component.onCompleted: {
        if (watchClient) {
            loadRecent()
        }
    }

    Connections {
        target: watchClient
        function onSignalsLoaded(signals) {
            root.signalsModel = signals
        }
        function onErrorOccurred(message) {
            console.warn("Watch signals error:", message)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Layout.fillWidth: true

            Button {
                text: "Refresh"
                onClicked: root.loadRecent()
            }

            Item { Layout.fillWidth: true }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.signalsModel

            delegate: ItemDelegate {
                width: ListView.view.width
                text: {
                    const stamp = modelData.timestamp || ""
                    const when = stamp.length > 0 ? new Date(stamp).toLocaleString() : ""
                    const name = modelData.ruleName || "Watch rule"
                    const severity = modelData.severity || "info"
                    const message = modelData.message || ""
                    return when + " • " + name + " [" + severity + "] " + message
                }
            }
        }
    }
}
