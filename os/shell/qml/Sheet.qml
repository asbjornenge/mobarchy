import QtQuick
import QtQuick.Window

// A layer surface that slides in and out under the finger: the app drawer and quick
// settings. It is only mapped while (partly) open. Subclasses lay out their content from
// `openness` and set the layer-shell properties.
LayerWindow {
    id: sheet

    shown: false
    color: "transparent"

    // 0 = closed, 1 = open
    property real openness: 0
    // How far the finger travels for 0 -> 1.
    property real travel: height > 0 ? height : Screen.height
    property real dragStart: 0
    // A finger has it.
    property bool dragging: false

    // Emitted when the sheet starts to come in.
    signal opening()

    function beginDrag() {
        settle.stop()
        dragging = true
        dragStart = shown ? openness : 0
        if (!shown) {
            shown = true
            opening()
        }
    }
    function dragTo(distance) {
        openness = Math.max(0, Math.min(1, dragStart + distance / travel))
    }
    // velocity: px/ms in the opening direction. A flick decides; otherwise how far it got
    // (a sheet that was open needs a longer pull to close than a closed one to open).
    function endDrag(velocity) {
        snap(velocity > 0.4 || (velocity > -0.4 && openness > (dragStart > 0.5 ? 0.7 : 0.3)))
    }
    function snap(open) {
        dragging = false
        settle.to = open ? 1 : 0
        settle.restart()
    }
    function open() {
        if (!shown) {
            shown = true
            opening()
        }
        snap(true)
    }
    function close() {
        if (shown)
            snap(false)
    }

    NumberAnimation {
        id: settle
        target: sheet
        property: "openness"
        duration: 260
        easing.type: Easing.OutCubic
        onFinished: if (sheet.openness === 0) sheet.shown = false
    }
}
