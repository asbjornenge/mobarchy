import QtQuick
import Mobarchy.Shell

// The battery, and the network the phone is on.
BoardCard {
    id: card

    required property SystemStatus system

    wanted: system.battery >= 0
    title: system.charging ? qsTr("Charging") : qsTr("Battery")
    icon: system.charging ? "󰂄" : "󰁹"

    compactValue: system.battery + "%"
    compactIconColor: level
    compactCaption: system.wifiConnected ? system.wifiName : qsTr("No Wi-Fi")

    readonly property color level: system.battery <= 15 && !system.charging ? Theme.red
                                 : system.charging ? Theme.green : Theme.accent

    Text {
        anchors { left: parent.left; top: parent.top; topMargin: -2 }
        text: card.system.battery + "%"
        color: Theme.fg
        font { family: Theme.font; pixelSize: 40; weight: Font.Light }
    }
    Column {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        spacing: 10
        Rectangle {
            width: parent.width
            height: 6
            radius: 3
            color: Theme.line
            Rectangle {
                width: parent.width * Math.max(0, Math.min(1, card.system.battery / 100))
                height: parent.height
                radius: 3
                color: card.level
            }
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.system.wifiConnected ? "󰖩  " + card.system.wifiName : "󰖪  " + qsTr("No Wi-Fi")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
    }
}
