pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// A number pad for a PIN (ADR 0013): dots for what is typed, 1–9 and 0, delete and enter.
// Touch, or (compact: dots only) laptop mode's keyboard, through the lock screen.
Item {
    id: pad

    property string title
    property string message           // under the dots: an error, a wait
    property bool messageIsError: false
    property bool enabled_: true      // false while checking or waiting
    property bool compact: false      // no number pad: typed on a keyboard
    property string pin: ""
    signal entered(string pin)

    function clear() { pin = "" }
    function shake() { shakeAnim.restart() }

    implicitWidth: 300
    implicitHeight: column.implicitHeight

    Column {
        id: column
        anchors.horizontalCenter: parent.horizontalCenter
        spacing: 22

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: pad.title
            color: Theme.fg
            font { family: Theme.font; pixelSize: 16 }
        }
        Item {
            id: dots
            anchors.horizontalCenter: parent.horizontalCenter
            width: 240
            height: 20
            Row {
                id: dotRow
                anchors.centerIn: parent
                spacing: 14
                Repeater {
                    model: Math.max(4, pad.pin.length)
                    delegate: Rectangle {
                        id: dot
                        required property int index
                        width: 12; height: 12; radius: 6
                        color: dot.index < pad.pin.length ? Theme.fg : "transparent"
                        border.color: Theme.muted
                        border.width: 1.5
                    }
                }
            }
            SequentialAnimation {
                id: shakeAnim
                NumberAnimation { target: dotRow; property: "anchors.horizontalCenterOffset"; to: -14; duration: 50 }
                NumberAnimation { target: dotRow; property: "anchors.horizontalCenterOffset"; to: 14; duration: 80 }
                NumberAnimation { target: dotRow; property: "anchors.horizontalCenterOffset"; to: -8; duration: 70 }
                NumberAnimation { target: dotRow; property: "anchors.horizontalCenterOffset"; to: 0; duration: 60 }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            height: 16
            text: pad.message
            color: pad.messageIsError ? Theme.red : Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
        Grid {
            visible: !pad.compact
            anchors.horizontalCenter: parent.horizontalCenter
            columns: 3
            columnSpacing: 22
            rowSpacing: 14
            opacity: pad.enabled_ ? 1 : 0.4
            Repeater {
                model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "⌫", "0", "↵"]
                delegate: Rectangle {
                    id: key
                    required property string modelData
                    readonly property bool digit: key.modelData >= "0" && key.modelData <= "9"
                    width: 72; height: 72; radius: 36
                    color: keyTap.pressed ? Theme.line : (digit ? Theme.panel : "transparent")
                    Text {
                        anchors.centerIn: parent
                        text: key.modelData === "⌫" ? "󰭜" : key.modelData === "↵" ? "󰄬" : key.modelData
                        color: key.modelData === "↵" ? Theme.accent : Theme.fg
                        font { family: Theme.font; pixelSize: key.digit ? 26 : 24 }
                    }
                    MouseArea {
                        id: keyTap
                        anchors.fill: parent
                        enabled: pad.enabled_
                        onClicked: {
                            if (key.digit) {
                                if (pad.pin.length < 16)
                                    pad.pin += key.modelData
                            } else if (key.modelData === "⌫") {
                                pad.pin = pad.pin.slice(0, -1)
                            } else if (pad.pin.length > 0) {
                                pad.entered(pad.pin)
                            }
                        }
                        onPressAndHold: if (key.modelData === "⌫") pad.pin = ""
                    }
                }
            }
        }
    }
}
