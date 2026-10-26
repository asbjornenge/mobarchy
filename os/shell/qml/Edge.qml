import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The bottom edge: swipe up for the open apps (the switcher), tap for home, and a keyboard
// toggle. (The installed apps are a page of home's, a swipe left from the widgets.) While the
// on-screen keyboard is up (always, in laptop mode), a tap is taken for a space bar that was
// missed: it doesn't go home, two taps do (a swipe up still opens the switcher).
LayerWindow {
    id: edge

    property real lastTap: 0    // (two taps go home while the keyboard is up)

    required property AppSwitcher switcher
    required property Keyboard keyboard
    // The strip below it that the panel's bezel hides (device.toml bottom_inset): the edge
    // reaches over it, so a swipe can start there, but shows its bar above it.
    property int inset: 0
    // laptop mode: its keyboard is always up; no toggle
    property bool laptop: false
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
        // (picked up: the switcher carries it under the finger)
        visible: !edge.switcher.shown
    }

    // The finger keeps its grab on this surface while it moves up over the switcher, which
    // follows it from the first few pixels up; let go, it stays or goes (AppSwitcher.release).
    property bool switching: false
    EdgeGesture {
        id: gesture
        anchors.fill: parent
        onBegan: {
            edge.switching = false
            edge.switcher.prepare(edge)
        }
        onMoved: (distance) => {
            if (edge.switching)
                edge.switcher.follow(distance)
            else if (distance > 12) {
                edge.switching = true
                edge.switcher.begin(distance)
            }
        }
        onEnded: (velocity) => {
            if (edge.switching)
                edge.switcher.release(velocity)
            edge.switching = false
        }
        onTapped: {
            const now = Date.now()
            if (!edge.keyboard.visible && !edge.laptop || now - edge.lastTap < 400)
                edge.home()
            edge.lastTap = now
        }
    }

    MouseArea {
        visible: !edge.laptop
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
