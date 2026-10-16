pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The SIM's PIN (ADR 0031): shown when the SIM is PIN-locked (pinRequired "pin"), in the
// Phone app and, later, on the lock screen. A PUK ("puk") needs the code and a new PIN;
// this does the PIN for now and tells the owner to use a PUK elsewhere if it comes to that.
Item {
    id: root
    required property var telephony

    property string code: ""

    Column {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 90 }
        spacing: 8
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.telephony.pinRequired === "puk" ? qsTr("SIM is PUK-locked") : qsTr("Enter SIM PIN")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 22 }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: root.telephony.pinRetries >= 0
            text: qsTr("%n try(s) left", "", root.telephony.pinRetries)
            color: root.telephony.pinRetries <= 1 ? Theme.red : Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
    }

    // the dots
    Row {
        id: dots
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 170 }
        spacing: 14
        Repeater {
            model: Math.max(4, root.code.length)
            delegate: Rectangle {
                required property int index
                width: 14; height: 14; radius: 7
                color: index < root.code.length ? Theme.accent : Theme.line
            }
        }
    }

    Grid {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 80 }
        columns: 3
        spacing: 18
        Repeater {
            model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "", "0", "⌫"]
            delegate: Rectangle {
                id: key
                required property string modelData
                width: 76; height: 76; radius: 38
                color: key.modelData === "" ? "transparent" : (tap.pressed ? Theme.line : Theme.panel)
                Text {
                    anchors.centerIn: parent
                    text: key.modelData
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: key.modelData === "⌫" ? 24 : 30 }
                }
                TapHandler {
                    id: tap
                    enabled: key.modelData !== ""
                    onTapped: {
                        if (key.modelData === "⌫")
                            root.code = root.code.slice(0, -1)
                        else {
                            root.code += key.modelData
                            if (root.code.length >= 4 && root.telephony.pinRequired === "pin"
                                && root.code.length === 4) {
                                // most SIM PINs are 4; submit, but let 5–8 be typed too
                            }
                        }
                    }
                }
            }
        }
    }

    // submit (PIN can be 4–8 digits)
    Rectangle {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 20 }
        width: 160; height: 44; radius: 22
        visible: root.code.length >= 4
        color: okTap.pressed ? Theme.line : Theme.accent
        Text { anchors.centerIn: parent; text: qsTr("Unlock"); color: Theme.bg; font { family: Theme.font; pixelSize: 16; bold: true } }
        TapHandler {
            id: okTap
            onTapped: { root.telephony.enterPin(root.code); root.code = "" }
        }
    }
}
