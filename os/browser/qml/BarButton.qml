import QtQuick

// A round icon button for the browser's bars: big enough for a thumb.
Item {
    id: b
    property string icon
    property string label            // shown under the icon in sheets
    property bool enabled_: true
    property color tint: Theme.fg
    signal clicked()
    implicitWidth: 52
    implicitHeight: label ? 64 : 52
    opacity: enabled_ ? 1 : 0.35

    Rectangle {
        id: disc
        anchors.horizontalCenter: parent.horizontalCenter
        y: 2
        width: 48; height: 48; radius: 24
        color: tap.pressed ? Theme.line : "transparent"
        Text {
            anchors.centerIn: parent
            text: b.icon
            color: b.tint
            font { family: Theme.font; pixelSize: 22 }
        }
    }
    Text {
        visible: b.label.length > 0
        anchors { horizontalCenter: parent.horizontalCenter; top: disc.bottom }
        text: b.label
        color: Theme.muted
        font { family: Theme.font; pixelSize: 11 }
    }
    MouseArea {
        id: tap
        anchors.fill: parent
        enabled: b.enabled_
        onClicked: b.clicked()
    }
}
