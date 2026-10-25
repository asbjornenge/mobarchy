import QtQuick

// Back (or forward), as on Android: a swipe in from the left edge (or the right). A bubble
// with an arrow follows the finger in, and lights up once it has come far enough; let go
// then, and `go` is emitted. With nowhere to go (enabled_ false) the bubble still comes,
// dimmed, and nothing happens.
// Put it over the page, along its edge. It takes the touches that start there from
// the first moment (a touch area, not a drag handler: the page's view keeps a touch it has
// been given, and a handler could never take it over).
Item {
    id: edge

    signal go()
    property bool enabled_: true
    // the right edge (forward) instead of the left (back)
    property bool fromRight: false
    // how far the finger has to come
    readonly property real far: 90

    x: fromRight ? parent.width - width : 0
    anchors { top: parent.top; bottom: parent.bottom }
    width: 28
    // how far the finger has come in
    function inward(t) { return fromRight ? t.startX - t.x : t.x - t.startX }
    readonly property real pulled: finger.pressed ? Math.max(0, inward(finger)) : 0
    readonly property bool enough: enabled_ && pulled >= far

    MultiPointTouchArea {
        anchors.fill: parent
        maximumTouchPoints: 1
        touchPoints: TouchPoint { id: finger }
        onReleased: if (edge.enabled_ && edge.inward(finger) >= edge.far) edge.go()
    }

    // the bubble, at the finger's height
    Rectangle {
        id: bubble
        readonly property real t: Math.min(1, edge.pulled / edge.far)
        visible: edge.pulled > 4
        width: 44; height: 44; radius: 22
        x: edge.fromRight ? edge.width - t * (width + 14) : -width + t * (width + 14)
        y: finger.y - height / 2
        color: edge.enough ? Theme.accent : Theme.panel
        border.color: Theme.line
        opacity: (edge.enabled_ ? 0.4 + 0.6 * t : 0.5 * t)
        scale: 0.7 + 0.3 * t
        Behavior on color { ColorAnimation { duration: 120 } }
        Text {
            anchors.centerIn: parent
            text: edge.fromRight ? "󰁔" : "󰁍"
            color: edge.enough ? Theme.bg : Theme.fg
            font { family: Theme.font; pixelSize: 22 }
        }
    }
}
