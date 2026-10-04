pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtMultimedia
import Qt.labs.folderlistmodel
import Mobarchy.Shell

// The camera (ADR 0018): the viewfinder, a shutter, the other camera, and the photos. In the
// camera app's window, and on the lock screen (locked: the photos are only the ones taken
// there). Frames come from the cameras' PipeWire sources, photos are the camera's own JPEGs
// (org.mobarchy.Camera), in ~/Pictures/Camera.
Rectangle {
    id: win

    color: "black"
    // the frames run while this is true (the view is visible and in front)
    property bool active: true
    property bool locked: false
    property var taken: []              // photos taken in this view (the locked gallery)
    signal closeRequested()

    property string error: ""
    property bool viewing: false        // the photos, instead of the viewfinder

    CameraSource {
        id: camera
        videoSink: viewfinder.videoSink
        // frames only while the viewfinder shows
        active: win.active && win.visible && !win.viewing
        deviceRotation: Screen.angleBetween(Screen.nativeOrientation, Screen.orientation)
        onFailed: (message) => { win.error = message; errorTimer.restart() }
        onPhotoTaken: (path) => { win.error = ""; win.taken = [path].concat(win.taken) }
    }

    // --- the viewfinder ------------------------------------------------------------------

    Item {
        id: finder
        anchors.fill: parent
        visible: !win.viewing

        // The sensor's image is rotated (sensor orientation); the front camera's mirrored, as
        // a mirror shows us (the photo is not).
        VideoOutput {
            id: viewfinder
            anchors.centerIn: parent
            readonly property bool sideways: camera.orientation % 180 !== 0
            width: sideways ? parent.height : parent.width
            height: sideways ? parent.width : parent.height
            fillMode: VideoOutput.PreserveAspectCrop
            rotation: camera.orientation
            transform: Scale {
                origin.x: viewfinder.width / 2
                xScale: camera.facing === "front" ? -1 : 1
            }
        }

        // the shutter's flash
        Rectangle {
            id: flash
            anchors.fill: parent
            color: "white"
            opacity: 0
            NumberAnimation on opacity { id: flashOut; from: 0.8; to: 0; duration: 250; running: false }
        }

        Text {
            anchors.centerIn: parent
            visible: camera.cameras.length === 0
            color: Theme.muted
            font { family: Theme.font; pixelSize: 16 }
            text: qsTr("Waiting for the cameras…")
        }
    }

    // --- the controls --------------------------------------------------------------------

    Rectangle {
        id: bar
        visible: !win.viewing
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 170
        color: "#99000000"

        // the last photo: opens the photos
        Rectangle {
            anchors { left: parent.left; leftMargin: 36; verticalCenter: parent.verticalCenter }
            width: 60; height: 60; radius: 12
            color: Theme.panel
            border.color: Theme.line
            clip: true
            Image {
                anchors.fill: parent
                // the newest: the one just taken, before the folder has caught up
                source: camera.lastPhoto !== "" ? "file://" + camera.lastPhoto
                      : photos.count > 0 ? "file://" + photos.get(0, "filePath") : ""
                sourceSize: Qt.size(120, 120)
                fillMode: Image.PreserveAspectCrop
                autoTransform: true
                asynchronous: true
            }
            TapHandler {
                enabled: photos.count > 0
                onTapped: { gallery.currentIndex = 0; win.viewing = true }
            }
        }

        // the shutter
        Rectangle {
            id: shutter
            anchors.centerIn: parent
            width: 84; height: 84; radius: width / 2
            color: "transparent"
            border { color: "white"; width: 5 }
            Rectangle {
                anchors.centerIn: parent
                width: shutterTap.pressed ? 58 : 66
                height: width; radius: width / 2
                color: camera.busy ? Theme.muted : "white"
                Behavior on width { NumberAnimation { duration: 80 } }
            }
            TapHandler {
                id: shutterTap
                enabled: !camera.busy && camera.current !== ""
                onTapped: { flashOut.restart(); camera.takePhoto() }
            }
        }

        // the other camera
        Rectangle {
            anchors { right: parent.right; rightMargin: 36; verticalCenter: parent.verticalCenter }
            width: 60; height: 60; radius: 30
            color: switchTap.pressed ? Theme.line : "#55ffffff"
            visible: camera.cameras.length > 1
            Text {
                anchors.centerIn: parent
                color: "white"
                font { family: Theme.font; pixelSize: 26 }
                text: "󰑓"
            }
            TapHandler { id: switchTap; onTapped: camera.switchCamera() }
        }
    }

    Rectangle {
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 60 }
        visible: win.error !== ""
        width: Math.min(parent.width - 48, errorText.implicitWidth + 32)
        height: errorText.implicitHeight + 20
        radius: 12
        color: Theme.panel
        border.color: Theme.red
        Text {
            id: errorText
            anchors.centerIn: parent
            width: parent.width - 32
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.fg
            font { family: Theme.font; pixelSize: 14 }
            text: win.error
        }
        Timer { id: errorTimer; interval: 6000; onTriggered: win.error = "" }
    }

    // --- the photos ----------------------------------------------------------------------

    FolderListModel {
        id: photos
        folder: "file://" + StandardPaths.writableLocation(StandardPaths.PicturesLocation)
                .toString().replace("file://", "") + "/Camera"
        // locked: only the photos taken here (or none)
        nameFilters: !win.locked ? ["*.jpg", "*.jpeg"]
                   : win.taken.length > 0 ? win.taken.map(p => p.split("/").pop()) : ["-"]
        sortField: FolderListModel.Time
        showDirs: false
    }

    ListView {
        id: gallery
        anchors.fill: parent
        visible: win.viewing
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        boundsBehavior: Flickable.StopAtBounds
        model: photos
        delegate: Image {
            required property string filePath
            width: gallery.width
            height: gallery.height
            source: "file://" + filePath
            sourceSize: Qt.size(gallery.width * 2, gallery.height * 2)
            fillMode: Image.PreserveAspectFit
            autoTransform: true
            asynchronous: true
        }
    }

    // locked: back to the lock screen
    Rectangle {
        visible: win.locked && !win.viewing
        anchors { left: parent.left; top: parent.top; margins: 28; topMargin: 48 }
        width: 52; height: 52; radius: 26
        color: lockTap.pressed ? Theme.line : "#55ffffff"
        Text {
            anchors.centerIn: parent
            color: "white"
            font { family: Theme.font; pixelSize: 22 }
            text: "󰅖"
        }
        TapHandler { id: lockTap; onTapped: win.closeRequested() }
    }

    // back to the viewfinder
    Rectangle {
        visible: win.viewing
        anchors { left: parent.left; bottom: parent.bottom; margins: 36 }
        width: 60; height: 60; radius: 30
        color: backTap.pressed ? Theme.line : "#55ffffff"
        Text {
            anchors.centerIn: parent
            color: "white"
            font { family: Theme.font; pixelSize: 26 }
            text: "󰄀"
        }
        TapHandler { id: backTap; onTapped: win.viewing = false }
    }
}
