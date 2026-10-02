import QtQuick
import Mobarchy.Shell

// An icon and a level from 0 to 1 that follows the finger (brightness, volume).
Item {
    id: slider

    property string icon
    property bool iconActive: false
    property real value: 0
    signal moved(real value)
    signal iconClicked()

    height: 40

    Text {
        id: iconText
        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
        width: 24
        text: slider.icon
        color: slider.iconActive ? Theme.accent : Theme.fg
        font { family: Theme.font; pixelSize: 20 }
        MouseArea {
            anchors { fill: parent; margins: -10 }
            onClicked: slider.iconClicked()
        }
    }
    Item {
        id: track
        anchors { left: iconText.right; leftMargin: 16; right: parent.right; rightMargin: 12
                  top: parent.top; bottom: parent.bottom }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width; height: 8; radius: 4
            color: Theme.panel
            Rectangle {
                width: slider.value * parent.width; height: parent.height
                radius: 4
                color: Theme.accent
            }
        }
        Rectangle {
            x: slider.value * track.width - width / 2
            anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 24; radius: 12
            color: Theme.fg
        }
        MouseArea {
            anchors { fill: parent; leftMargin: -12; rightMargin: -12 }
            preventStealing: true
            function set(m) { slider.moved(Math.max(0, Math.min(1, (m.x - 12) / track.width))) }
            onPressed: (m) => set(m)
            onPositionChanged: (m) => set(m)
        }
    }
}
