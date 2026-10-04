import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// A black strip along the left, right or bottom edge that apps keep clear of: where the
// panel runs under the bezel (device.toml side_inset), or its camera cutout when the screen
// is turned (top_inset; the status bar covers it at the top).
LayerWindow {
    id: strip

    required property string edge       // "left", "right" or "bottom"
    required property int inset

    shown: inset > 0
    width: Math.max(1, inset)
    height: Math.max(1, inset)
    color: "black"
    LayerShell.Window.scope: "mobarchy-inset"
    LayerShell.Window.layer: LayerShell.Window.LayerBottom
    LayerShell.Window.anchors: edge === "bottom"
        ? LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
        : LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
          | (edge === "left" ? LayerShell.Window.AnchorLeft : LayerShell.Window.AnchorRight)
    LayerShell.Window.exclusionZone: inset
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
}
