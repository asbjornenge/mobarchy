import QtQuick

// Back, as on Android: a swipe in from the left edge. A bubble with an arrow follows the
// finger in, and lights up once it has come far enough; let go then, and `back` is emitted.
// With nowhere to go back to (enabled_ false) the bubble still comes, dimmed, and nothing
// happens.
// Put it over the page, along its left edge. It takes the touches that start there from
// the first moment (a touch area, not a drag handler: the page's view keeps a touch it has
// been given, and a handler could never take it over).
Item {
    id: edge

    signal back()
    property bool enabled_: true
    // how far the finger has to come
    readonly property real far: 90

    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
    width: 28
    readonly property real pulled: finger.pressed ? Math.max(0, finger.x - finger.startX) : 0
    readonly property bool enough: enabled_ && pulled >= far

    MultiPointTouchArea {
        anchors.fill: parent
        maximumTouchPoints: 1
        touchPoints: TouchPoint { id: finger }
        onReleased: if (edge.enabled_ && finger.x - finger.startX >= edge.far) edge.back()
    }

    // the bubble, at the finger's height
    Rectangle {
        id: bubble
        readonly property real t: Math.min(1, edge.pulled / edge.far)
        visible: edge.pulled > 4
        width: 44; height: 44; radius: 22
        x: -width + t * (width + 14)
        y: finger.y - height / 2
        color: edge.enough ? Theme.accent : Theme.panel
        border.color: Theme.line
        opacity: (edge.enabled_ ? 0.4 + 0.6 * t : 0.5 * t)
        scale: 0.7 + 0.3 * t
        Behavior on color { ColorAnimation { duration: 120 } }
        Text {
            anchors.centerIn: parent
            text: "󰁍"
            color: edge.enough ? Theme.bg : Theme.fg
            font { family: Theme.font; pixelSize: 22 }
        }
    }
}
