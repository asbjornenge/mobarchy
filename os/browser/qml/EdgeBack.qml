import QtQuick

// Back, as on Android: a swipe in from the left edge. A bubble with an arrow follows the
// finger in, and lights up once it has come far enough; let go then, and `back` is emitted.
// Put it over the page, along its left edge (it takes the touches that start there).
Item {
    id: edge

    signal back()
    property bool enabled_: true
    // how far the finger has to come
    readonly property real far: 90

    anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
    width: 28
    readonly property real pulled: drag.active ? Math.max(0, drag.centroid.position.x - drag.centroid.pressPosition.x) : 0
    readonly property bool enough: pulled >= far

    DragHandler {
        id: drag
        enabled: edge.enabled_
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onActiveChanged: if (!active && edge.lastPulled >= edge.far) edge.back()
    }
    // (the last pull, as the drag ends: `pulled` is 0 by then)
    property real lastPulled: 0
    onPulledChanged: if (drag.active) lastPulled = pulled

    // the bubble, at the finger's height
    Rectangle {
        id: bubble
        readonly property real t: Math.min(1, edge.pulled / edge.far)
        visible: drag.active && edge.pulled > 4
        width: 44; height: 44; radius: 22
        x: -width + t * (width + 14)
        y: drag.centroid.position.y - height / 2
        color: edge.enough ? Theme.accent : Theme.panel
        border.color: Theme.line
        opacity: 0.4 + 0.6 * t
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
