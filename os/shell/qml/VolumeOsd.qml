import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The volume, shown for a moment when the volume keys are pressed.
LayerWindow {
    id: osd

    required property Volume volume

    shown: false
    width: 300
    height: 72
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-osd"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop
    // Below the status bar (it respects the bar's exclusive zone).
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    function flash() {
        shown = true
        hideTimer.restart()
    }
    Timer { id: hideTimer; interval: 1500; onTriggered: osd.shown = false }

    Rectangle {
        anchors { fill: parent; topMargin: 16 }
        radius: height / 2
        color: Theme.panel
        border.color: Theme.line

        Text {
            id: icon
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            text: osd.volume.muted || osd.volume.volume === 0 ? "󰖁"
                : osd.volume.volume < 0.34 ? "󰕿" : osd.volume.volume < 0.67 ? "󰖀" : "󰕾"
            color: Theme.fg
            font { family: Theme.font; pixelSize: 20 }
        }
        Rectangle {
            id: bar
            anchors { left: icon.right; leftMargin: 14; right: percent.left; rightMargin: 14
                      verticalCenter: parent.verticalCenter }
            height: 8; radius: 4
            color: Theme.line
            Rectangle {
                height: parent.height; radius: 4
                width: (osd.volume.muted ? 0 : osd.volume.volume) * parent.width
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 80 } }
            }
        }
        Text {
            id: percent
            anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
            width: 40
            horizontalAlignment: Text.AlignRight
            text: osd.volume.muted ? "" : Math.round(osd.volume.volume * 100)
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
    }
}
