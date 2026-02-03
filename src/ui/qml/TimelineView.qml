import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    property var events: []

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        visible: !(root.events && root.events.length > 0)
        text: "No events in this period."
        explanation: "Adjust the time range or try again later."
    }

    ListView {
        id: listView
        anchors.fill: parent
        visible: root.events && root.events.length > 0
        model: root.events
        spacing: Kirigami.Units.smallSpacing
        clip: true

        delegate: EventCard {
            width: listView.width
            eventData: modelData
        }
    }
}
