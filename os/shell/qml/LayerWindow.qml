import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell

// A layer-shell surface that survives output changes. When its output goes away (a
// fold), the compositor dismisses the surface and layer-shell-qt closes the window; it is
// shown again, on the output that is active now.
Window {
    id: root

    // Whether the surface should be up. Set this, not `visible`.
    property bool shown: true

    LayerShell.Window.wantsToBeOnActiveScreen: true
    LayerShell.Window.activateOnShow: false

    onShownChanged: shown ? show() : hide()
    Component.onCompleted: if (shown) show()
    onVisibleChanged: if (!visible && shown) restore.restart()

    Timer {
        id: restore
        interval: 200
        onTriggered: if (root.shown && !root.visible) root.show()
    }
}
