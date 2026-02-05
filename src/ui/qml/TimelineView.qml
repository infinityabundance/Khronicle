import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Item {
    id: root
    property var events: []
    signal eventClicked(var eventData)

    Kirigami.PlaceholderMessage {
        anchors.centerIn: parent
        visible: !(root.events && root.events.length > 0)
        text: "No events matching the current filters."
        explanation: "Try changing the categories or date range."
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
            onClicked: {
                listView.currentIndex = index
                root.eventClicked(eventData)
            }
        }
    }
}
