import QtQuick
import Mobarchy.Shell

// A battery, drawn: an outline with a nub, filled as far as it is charged (a glyph only has
// steps). Charging, a lighter band keeps filling it up from the charge to the top, and a bolt
// sits on it; low and not charging, it is red.
Item {
    id: icon

    property int level: 0          // percent
    property bool charging: false
    // it may move (the screen it is on is in sight)
    property bool animate: true
    property color tint: level <= 15 && !charging ? Theme.red : charging ? Theme.green : Theme.fg

    implicitWidth: 30
    implicitHeight: 15

    readonly property real nub: Math.max(2, width * 0.08)
    readonly property real line: Math.max(1.5, height * 0.11)
    readonly property real inner: body.width - 4 * line

    Rectangle {
        id: body
        width: icon.width - icon.nub - 1
        height: icon.height
        radius: height * 0.28
        color: "transparent"
        border.color: Qt.alpha(icon.tint, 0.85)
        border.width: icon.line

        // the charge
        Rectangle {
            id: fill
            x: 2 * icon.line; y: 2 * icon.line
            height: parent.height - 4 * icon.line
            width: icon.inner * Math.max(0.04, Math.min(1, icon.level / 100))
            radius: Math.max(1, body.radius - 2 * icon.line)
            color: icon.tint
            Behavior on width { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
        }
        // charging: filling up, again and again
        Rectangle {
            id: rising
            visible: icon.charging && icon.level < 100
            x: fill.x + fill.width
            y: fill.y
            height: fill.height
            radius: fill.radius
            color: icon.tint
            width: 0
            opacity: 0.5
            SequentialAnimation {
                running: rising.visible && icon.animate
                loops: Animation.Infinite
                onStopped: { rising.width = 0; rising.opacity = 0.5 }
                ParallelAnimation {
                    NumberAnimation { target: rising; property: "width"; from: 0; to: icon.inner - fill.width
                                      duration: 1400; easing.type: Easing.InOutQuad }
                    SequentialAnimation {
                        PropertyAction { target: rising; property: "opacity"; value: 0.55 }
                        PauseAnimation { duration: 900 }
                        NumberAnimation { target: rising; property: "opacity"; to: 0; duration: 500 }
                    }
                }
                PauseAnimation { duration: 500 }
            }
        }
        Text {
            visible: icon.charging
            anchors.centerIn: parent
            text: "󱐋"
            color: Theme.bg
            style: Text.Outline
            styleColor: icon.tint
            font { family: Theme.font; pixelSize: icon.height * 0.95 }
        }
    }
    Rectangle {
        anchors { left: body.right; leftMargin: 1; verticalCenter: body.verticalCenter }
        width: icon.nub
        height: icon.height * 0.42
        radius: width / 2
        color: Qt.alpha(icon.tint, 0.85)
    }
}
