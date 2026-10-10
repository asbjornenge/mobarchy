import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The bottom edge: swipe up for the app drawer, swipe up and hold for the open apps (the
// switcher), tap for home, and a keyboard toggle. While
// the on-screen keyboard is up, a tap is taken for a space bar that was missed: it doesn't
// go home (a swipe up still opens the drawer).
LayerWindow {
    id: edge

    required property Drawer drawer
    required property AppSwitcher switcher
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
        // (picked up: the drawer's handle and the switcher carry it under the finger)
        visible: !edge.drawer.shown && !edge.switcher.shown
    }

    // The finger keeps its grab on this surface while it moves up over the drawer.
    // A finger that stops a little way up (and stays a moment) asks for the switcher; one
    // that goes on, or flicks, for the drawer.
    property real distance: 0
    property bool switching: false
    Timer {
        id: hold
        interval: 220
        onTriggered: {
            // (against the drawer's height, which is up under the finger: Screen.height can be
            // a placeholder's, left from an output change, a fold)
            if (edge.distance < 70 || edge.distance > 0.45 * edge.drawer.height)
                return
            edge.switching = true
            edge.switcher.begin(edge.distance)
        }
    }
    EdgeGesture {
        id: gesture
        anchors.fill: parent
        onBegan: {
            edge.switching = false
            edge.distance = 0
            edge.switcher.prepare(edge)
            edge.drawer.beginDrag()
        }
        onMoved: (distance) => {
            // (the drawer stays under the finger, behind the switcher, until it lets go)
            edge.drawer.dragTo(distance)
            if (edge.switching) {
                edge.switcher.follow(distance)
                return
            }
            if (Math.abs(distance - edge.distance) > 6)
                hold.restart()
            edge.distance = distance
        }
        onEnded: (velocity) => {
            hold.stop()
            if (edge.switching) {
                edge.switcher.release(velocity)
                edge.drawer.snap(false)
            } else
                edge.drawer.endDrag(velocity)
        }
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
