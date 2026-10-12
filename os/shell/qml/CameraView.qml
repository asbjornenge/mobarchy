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
    // photos or videos: what the shutter does
    property string mode: "photo"       // photo, video
    function isVideo(path: string): bool { return path.endsWith(".mp4") }
    // a video's thumbnail (the camera source makes one: the freedesktop thumbnail spec)
    function thumbnail(path: string): string {
        return StandardPaths.writableLocation(StandardPaths.GenericCacheLocation)
               + "/thumbnails/large/" + Qt.md5("file://" + path) + ".png"
    }
    // what shows: the viewfinder, or the photos (the gallery: a grid, or one photo)
    property string view: "finder"      // finder, gallery
    readonly property bool viewing: view !== "finder"
    // Unlocked, the camera's photos are the Photos app's (its Camera album); locked, only the
    // ones taken here, in the camera's own gallery.
    signal photosRequested(string path)

    function showPhoto(index) {
        view = "gallery"
        gallery.show(index)
    }

    DeviceOrientation {
        id: orientation
        active: camera.active
    }

    CameraSource {
        id: camera
        videoSink: viewfinder.videoSink
        // frames only while the viewfinder shows
        active: win.active && win.visible && !win.viewing
        videoMode: win.mode === "video"
        // photos stand as the device is held, whether the screen turns or not
        deviceRotation: orientation.degrees
        onFailed: (message) => { win.error = message; errorTimer.restart() }
        onPhotoTaken: (path) => { win.error = ""; win.taken = [path].concat(win.taken) }
        onVideoRecorded: (path) => { win.error = ""; win.taken = [path].concat(win.taken) }
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
            // the whole of what the camera takes: photos 4:3, videos 16:9
            fillMode: VideoOutput.PreserveAspectFit
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
            // the newest: the one just taken, before the folder has caught up
            readonly property string newest: camera.lastPhoto !== "" ? camera.lastPhoto
                                           : photos.count > 0 ? photos.get(0, "filePath") : ""
            Image {
                anchors.fill: parent
                source: parent.newest === "" ? ""
                      : win.isVideo(parent.newest) ? win.thumbnail(parent.newest)
                      : "file://" + parent.newest
                sourceSize: Qt.size(120, 120)
                fillMode: Image.PreserveAspectCrop
                autoTransform: true
                asynchronous: true
                cache: false
            }
            Text {
                anchors.centerIn: parent
                visible: win.isVideo(parent.newest)
                color: "white"
                style: Text.Outline
                font { family: Theme.font; pixelSize: 22 }
                text: "󰐊"
            }
            TapHandler {
                enabled: photos.count > 0 && !camera.recording
                onTapped: win.locked ? win.showPhoto(0) : win.photosRequested(parent.newest)
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
                // a photo: white; a video: a red dot, a red square while recording
                width: camera.recording ? 34 : shutterTap.pressed ? 58 : 66
                height: width
                radius: camera.recording ? 8 : width / 2
                color: camera.busy ? Theme.muted : win.mode === "video" ? Theme.red : "white"
                Behavior on width { NumberAnimation { duration: 80 } }
                Behavior on radius { NumberAnimation { duration: 80 } }
            }
            TapHandler {
                id: shutterTap
                enabled: !camera.busy && camera.current !== ""
                onTapped: {
                    if (win.mode === "photo") {
                        flashOut.restart()
                        camera.takePhoto()
                    } else if (camera.recording) {
                        camera.stopRecording()
                    } else {
                        camera.startRecording()
                    }
                }
            }
        }

        // photo or video
        Row {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: shutter.top; bottomMargin: 14 }
            spacing: 24
            visible: !camera.recording
            Repeater {
                model: [{mode: "photo", label: qsTr("Photo")}, {mode: "video", label: qsTr("Video")}]
                delegate: Text {
                    id: modeLabel
                    required property var modelData
                    text: modelData.label
                    color: win.mode === modelData.mode ? Theme.accent : "white"
                    font { family: Theme.font; pixelSize: 14; bold: win.mode === modelData.mode }
                    TapHandler {
                        onTapped: win.mode = modeLabel.modelData.mode
                    }
                }
            }
        }

        // how long the video has been recording
        Text {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: shutter.top; bottomMargin: 14 }
            visible: camera.recording
            color: Theme.red
            font { family: Theme.font; pixelSize: 16; bold: true }
            property int seconds: 0
            text: "● " + Math.floor(seconds / 60) + ":" + String(seconds % 60).padStart(2, "0")
            Timer {
                interval: 1000
                repeat: true
                running: camera.recording
                onRunningChanged: if (running) parent.seconds = 0
                onTriggered: parent.seconds++
            }
        }

        // the other camera
        Rectangle {
            anchors { right: parent.right; rightMargin: 36; verticalCenter: parent.verticalCenter }
            width: 60; height: 60; radius: 30
            color: switchTap.pressed ? Theme.line : "#55ffffff"
            visible: camera.cameras.length > 1 && !camera.recording
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
        nameFilters: !win.locked ? ["*.jpg", "*.jpeg", "*.mp4"]
                   : win.taken.length > 0 ? win.taken.map(p => p.split("/").pop()) : ["-"]
        sortField: FolderListModel.Time
        showDirs: false
    }

    // the photos: the same gallery as the Photos app's (Gallery.qml)
    Gallery {
        id: gallery
        anchors.fill: parent
        visible: win.viewing
        model: photos
        gridTop: 96
        gridBottom: 120
        remove: (path) => {
            if (!camera.deletePhoto(path))
                return false
            win.taken = win.taken.filter(p => p !== path)
            if (photos.count <= 1)          // that was the last one
                Qt.callLater(() => win.view = "finder")
            return true
        }
    }
    // the grid's way back to the camera
    Rectangle {
        visible: win.viewing && gallery.view === "grid"
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 36 }
        width: 60; height: 60; radius: 30
        color: backTap.pressed ? Theme.line : "#55ffffff"
        Text {
            anchors.centerIn: parent
            color: "white"
            font { family: Theme.font; pixelSize: 26 }
            text: "󰄀"
        }
        TapHandler { id: backTap; onTapped: win.view = "finder" }
    }
    Text {
        anchors.centerIn: parent
        visible: win.viewing && gallery.view === "grid" && photos.count === 0
        color: Theme.muted
        font { family: Theme.font; pixelSize: 16 }
        text: qsTr("No photos yet")
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
}
