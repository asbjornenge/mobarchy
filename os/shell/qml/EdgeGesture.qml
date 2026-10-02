import QtQuick

// A one-axis drag that drives a Sheet. Positions are taken in window coordinates, so it
// also works on an item that moves under the finger (a sheet's handle) and after the
// finger has left the item (the touch keeps its grab).
MouseArea {
    id: gesture

    // +1: the sheet opens with an upward drag (it comes from the bottom); -1: downward.
    property int direction: 1
    property real threshold: 8

    signal began()
    signal moved(real distance)   // px in the opening direction since the press
    signal ended(real velocity)   // px/ms in the opening direction, at release
    signal tapped()

    property real startY: 0
    property real lastY: 0
    property real lastTime: 0
    property real velocity: 0
    property bool dragging: false

    function windowY(m) { return mapToItem(null, m.x, m.y).y }

    onPressed: (m) => {
        startY = lastY = windowY(m)
        lastTime = Date.now()
        velocity = 0
        dragging = false
    }
    onPositionChanged: (m) => {
        const y = windowY(m), now = Date.now()
        // Smoothed, so one late event does not decide a flick.
        velocity = 0.6 * velocity + 0.4 * direction * (lastY - y) / Math.max(1, now - lastTime)
        lastY = y
        lastTime = now
        const distance = direction * (startY - y)
        if (!dragging && Math.abs(distance) > threshold) {
            dragging = true
            began()
        }
        if (dragging)
            moved(distance)
    }
    onReleased: dragging ? ended(velocity) : tapped()
    onCanceled: if (dragging) ended(0)
}
