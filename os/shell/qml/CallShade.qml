import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// During a call at the ear (the proximity sensor near): the screen black, and touches taken
// here so a cheek presses nothing. The display stays on and its touchscreen with it: turned
// off, the touchscreen stops reporting, and the cheek's touches were never let go (the
// compositor kept them down after the call, and nothing but the Phone app took a tap).
LayerWindow {
    id: shade

    shown: false
    color: "black"
    LayerShell.Window.scope: "mobarchy-callshade"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    MultiPointTouchArea { anchors.fill: parent }
    MouseArea { anchors.fill: parent }
}
