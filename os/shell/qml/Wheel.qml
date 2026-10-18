import QtQuick
import Mobarchy.Shell

// A number wheel (hours, minutes): flick it round, the middle one is chosen. 0..count-1,
// shown with two digits; it wraps.
PathView {
    id: wheel

    property int count: 60
    property int value: 0
    property int step: 1    // what each row shows: its index times this (minutes by fives)
    readonly property int rowHeight: 52

    width: 92
    height: rowHeight * 5
    clip: true
    model: count
    pathItemCount: 5
    preferredHighlightBegin: 0.5
    preferredHighlightEnd: 0.5
    highlightRangeMode: PathView.StrictlyEnforceRange
    snapMode: PathView.SnapToItem
    flickDeceleration: Theme.flickDeceleration
    maximumFlickVelocity: Theme.flickVelocity
    currentIndex: value
    onCurrentIndexChanged: if (currentIndex !== value) value = currentIndex
    onValueChanged: if (currentIndex !== value) currentIndex = value

    path: Path {
        startX: wheel.width / 2; startY: 0
        PathLine { x: wheel.width / 2; y: wheel.height }
    }

    delegate: Text {
        id: digit
        required property int index
        width: wheel.width
        height: wheel.rowHeight
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: (index * wheel.step < 10 ? "0" : "") + index * wheel.step
        // (the middle one bright and big, the others fading)
        readonly property real off: Math.abs(y + height / 2 - wheel.height / 2) / wheel.rowHeight
        opacity: Math.max(0.15, 1 - off * 0.4)
        scale: Math.max(0.7, 1 - off * 0.12)
        color: off < 0.5 ? Theme.fg : Theme.muted
        font { family: Theme.font; pixelSize: 40; weight: Font.Light }
    }

    // the band the chosen one sits in
    Rectangle {
        z: -1
        anchors.centerIn: parent
        width: parent.width
        height: wheel.rowHeight
        radius: 12
        color: Theme.panel
    }
}
