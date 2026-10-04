import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The bottom edge: swipe up for the app drawer, tap for home, and a keyboard toggle. While
// the on-screen keyboard is up, a tap is taken for a space bar that was missed: it doesn't
// go home (a swipe up still opens the drawer).
LayerWindow {
    id: edge

    required property Drawer drawer
    required property Keyboard keyboard
    // The strip below it that the panel's bezel hides (device.toml bottom_inset): the edge
    // reaches over it, so a swipe can start there, but shows its bar above it.
    property int inset: 0
    signal home()

    height: 32 + inset
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-edge"
    // The bottom layer is arranged first, so the edge keeps the very bottom of the screen
    // and the on-screen keyboard (top layer) sits above it, not under the finger.
    LayerShell.Window.layer: LayerShell.Window.LayerBottom
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: height
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    Rectangle {
        anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.top; verticalCenterOffset: 16 }
        width: 140; height: 5; radius: 2.5
        color: gesture.pressed ? Theme.accent : Theme.muted
    }

    // The finger keeps its grab on this surface while it moves up over the drawer.
    EdgeGesture {
        id: gesture
        anchors.fill: parent
        onBegan: edge.drawer.beginDrag()
        onMoved: (distance) => edge.drawer.dragTo(distance)
        onEnded: (velocity) => edge.drawer.endDrag(velocity)
        onTapped: if (!edge.keyboard.visible) edge.home()
    }

    MouseArea {
        anchors { right: parent.right; top: parent.top }
        height: 32
        width: 72
        onClicked: edge.keyboard.toggle()
        Text {
            anchors.centerIn: parent
            text: "󰌌"
            color: edge.keyboard.visible ? Theme.accent : Theme.muted
            font { family: Theme.font; pixelSize: 20 }
        }
    }
}
