import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// A screenshot was taken: the screen flashes white for a moment.
LayerWindow {
    id: flash

    shown: false
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-flash"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    function go() {
        white.opacity = 0.8
        shown = true
        fade.restart()
    }

    Rectangle {
        id: white
        anchors.fill: parent
        color: "white"
    }
    SequentialAnimation {
        id: fade
        PauseAnimation { duration: 60 }
        NumberAnimation { target: white; property: "opacity"; to: 0; duration: 260; easing.type: Easing.OutQuad }
        ScriptAction { script: flash.shown = false }
    }
}
