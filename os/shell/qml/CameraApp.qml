import QtQuick
import Mobarchy.Shell

// The camera app (ADR 0018): its own window (mobarchy-shell --camera), opened from the quick
// settings. The lock screen has the same camera (CameraView), locked.
Window {
    id: win

    visible: true
    title: qsTr("Camera")
    color: "black"

    CameraView {
        anchors.fill: parent
        active: win.active
        onCloseRequested: win.close()
        onPhotosRequested: (path) => Photos.open(path)
    }
}
