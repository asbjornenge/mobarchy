import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The SIM's PIN, asked by the shell itself (ADR 0031): after a start the SIM is locked, and
// calls and messages wait for it. Over everything, the lock screen included (as other phones
// do: the SIM's PIN is not the owner's), until it is given or put off ("Later": the Phone
// app asks again).
LayerWindow {
    id: sim

    required property Telephony telephony
    property bool later: false

    shown: !later && telephony.available && telephony.simPresent
           && (telephony.pinRequired === "pin" || telephony.pinRequired === "puk")
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-simpin"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    SimPin {
        anchors.fill: parent
        telephony: sim.telephony
    }
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 40 }
        text: qsTr("Later")
        color: Theme.accent
        font { family: Theme.font; pixelSize: 15 }
        padding: 12
        TapHandler { onTapped: sim.later = true }
    }
}
