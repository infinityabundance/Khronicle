import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import org.kde.kirigami as Kirigami

Kirigami.Page {
    id: root
    title: "Watchpoints"

    ColumnLayout {
        anchors.fill: parent
        spacing: Kirigami.Units.largeSpacing

        TabBar {
            id: tabs
            Layout.fillWidth: true

            TabButton { text: "Rules" }
            TabButton { text: "Signals" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabs.currentIndex

            WatchRulesPage { }
            WatchSignalsPage { }
        }
    }
}
