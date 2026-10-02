import QtQuick
import QtQuick.Window
import Mobarchy.Shell

// The passphrase for a new network. A normal window, not part of quick settings: on
// Hyprland only normal windows get on-screen keyboard input (ADR 0006).
Window {
    id: dialog

    required property Wifi wifi
    property string ssid: ""

    title: qsTr("Wi-Fi password")
    color: Theme.bg

    function ask(network) {
        ssid = network
        field.text = ""
        reveal.checked = false
        show()
        field.forceActiveFocus()
    }
    function submit() {
        if (!dialog.wifi.validPassphrase(field.text))
            return
        dialog.wifi.joinWithPassphrase(dialog.ssid, field.text)
        field.text = ""
        close()
    }

    Column {
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24; topMargin: 32 }
        spacing: 18

        Text {
            text: "󰖩  " + dialog.ssid
            color: Theme.fg
            font { family: Theme.font; pixelSize: 20; bold: true }
            elide: Text.ElideRight
            width: parent.width
        }
        Text {
            text: qsTr("Password")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
        Rectangle {
            width: parent.width
            height: 52
            radius: 14
            color: Theme.panel
            border.color: field.activeFocus ? Theme.accent : Theme.line
            TextInput {
                id: field
                anchors { left: parent.left; right: eye.left; leftMargin: 16; rightMargin: 8
                          verticalCenter: parent.verticalCenter }
                clip: true
                color: Theme.fg
                echoMode: reveal.checked ? TextInput.Normal : TextInput.Password
                passwordCharacter: "•"
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                font { family: Theme.font; pixelSize: 16 }
                onAccepted: dialog.submit()
            }
            MouseArea {
                id: eye
                anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
                width: 52
                onClicked: reveal.checked = !reveal.checked
                QtObject { id: reveal; property bool checked: false }
                Text {
                    anchors.centerIn: parent
                    text: reveal.checked ? "󰈈" : "󰈉"
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 18 }
                }
            }
        }
        Text {
            visible: field.text.length > 0 && !dialog.wifi.validPassphrase(field.text)
            text: qsTr("8 to 63 characters")
            color: Theme.red
            font { family: Theme.font; pixelSize: 12 }
        }
        Row {
            spacing: 12
            anchors.right: parent.right
            Rectangle {
                width: 120; height: 46; radius: 23
                color: Theme.panel
                border.color: Theme.line
                Text { anchors.centerIn: parent; text: qsTr("Cancel"); color: Theme.fg
                       font { family: Theme.font; pixelSize: 14 } }
                MouseArea { anchors.fill: parent; onClicked: { field.text = ""; dialog.close() } }
            }
            Rectangle {
                width: 140; height: 46; radius: 23
                color: dialog.wifi.validPassphrase(field.text) ? Theme.accent : Theme.line
                Text { anchors.centerIn: parent; text: qsTr("Connect"); color: Theme.bg
                       font { family: Theme.font; pixelSize: 14; bold: true } }
                MouseArea { anchors.fill: parent; onClicked: dialog.submit() }
            }
        }
    }
}
