import QtQuick
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    title: "Khronicle"
    width: 640
    height: 480

    Component.onCompleted: {
        khronicleApi.connectToDaemon()
        const now = new Date()
        const yesterday = new Date(now.getTime() - 24 * 3600 * 1000)
        khronicleApi.loadSummarySince(yesterday)
    }

    Connections {
        target: khronicleApi
        function onSummaryLoaded(summary) {
            console.log("Summary:", summary)
        }
        function onErrorOccurred(message) {
            console.warn("Khronicle API error:", message)
        }
    }

    Kirigami.Label {
        anchors.centerIn: parent
        text: "Khronicle UI stub"
    }
}
