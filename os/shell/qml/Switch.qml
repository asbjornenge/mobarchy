import QtQuick
import Mobarchy.Shell

// An on/off switch in the theme.
Rectangle {
    id: sw

    property bool checked: false
    signal toggled()

    width: 48; height: 28
    radius: height / 2
    color: checked ? Theme.accent : Theme.line
    Behavior on color { ColorAnimation { duration: 120 } }

    Rectangle {
        width: 22; height: 22
        radius: 11
        anchors.verticalCenter: parent.verticalCenter
        x: sw.checked ? sw.width - width - 3 : 3
        color: Theme.fg
        Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    }
    MouseArea {
        anchors { fill: parent; margins: -8 }
        onClicked: sw.toggled()
    }
}
