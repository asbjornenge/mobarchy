import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// polkit asks for the owner (ADR 0024): what for (with pkexec, the command), then a finger
// on the sensor or the PIN. Over everything; not while the phone is locked (the
// request waits for the unlock).
LayerWindow {
    id: sheet

    required property PolkitAgent polkit
    required property Haptics haptics

    shown: polkit.active && !polkit.held
    onShownChanged: {
        pad.clear()
        if (shown)
            haptics.click()
    }
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-auth"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    Connections {
        target: sheet.polkit
        function onFailed() { pad.clear(); pad.shake() }
        function onAuthorized() { sheet.haptics.click() }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgDark
        MouseArea { anchors.fill: parent }
    }

    Column {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28; topMargin: 56 }
        spacing: 14

        Text {
            text: "󰒃  " + qsTr("Authorize")
            color: Theme.accent
            font { family: Theme.font; pixelSize: 15; bold: true }
        }
        Text {
            width: parent.width
            text: sheet.polkit.message
            color: Theme.fg
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 17 }
        }
        Text {
            visible: sheet.polkit.fingerWanted
            width: parent.width
            text: "󰈷  " + qsTr("Touch the sensor, or enter your PIN")
            color: Theme.muted
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 14 }
        }
    }

    PinPad {
        id: pad
        anchors { horizontalCenter: parent.horizontalCenter; bottom: cancel.top; bottomMargin: 28 }
        title: qsTr("PIN")
        message: sheet.polkit.busy ? qsTr("Checking …") : sheet.polkit.error
        messageIsError: !sheet.polkit.busy && sheet.polkit.error.length > 0
        enabled_: !sheet.polkit.busy
        onEntered: (pin) => sheet.polkit.enterPin(pin)
    }

    Text {
        id: cancel
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 48 }
        text: qsTr("Cancel")
        color: Theme.muted
        font { family: Theme.font; pixelSize: 15 }
        MouseArea {
            anchors { fill: parent; margins: -16 }
            onClicked: sheet.polkit.cancel()
        }
    }
}
