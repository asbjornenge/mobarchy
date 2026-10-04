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
    // what shows: the viewfinder, the photos in a grid, or one photo (swipe for the others)
    property string view: "finder"      // finder, grid, photo
    readonly property bool viewing: view !== "finder"

    function showPhoto(index) {
        gallery.positionViewAtIndex(index, ListView.Beginning)
        gallery.currentIndex = index
        view = "photo"
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
        // photos stand as the device is held, whether the screen turns or not
        deviceRotation: orientation.degrees
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
                onTapped: win.showPhoto(0)
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

    // one photo at a time, newest first; swipe for the others
    ListView {
        id: gallery
        anchors.fill: parent
        visible: win.view === "photo"
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

    // all of them, as a grid of thumbnails
    GridView {
        id: grid
        anchors { fill: parent; topMargin: 96; bottomMargin: 120; leftMargin: 4; rightMargin: 4 }
        visible: win.view === "grid"
        clip: true
        readonly property int columns: Math.max(3, Math.floor(width / 130))
        cellWidth: width / columns
        cellHeight: cellWidth
        model: photos
        boundsBehavior: Flickable.StopAtBounds
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        delegate: Item {
            id: cell
            required property string filePath
            required property int index
            width: grid.cellWidth
            height: grid.cellHeight
            Image {
                anchors { fill: parent; margins: 2 }
                source: "file://" + cell.filePath
                sourceSize: Qt.size(grid.cellWidth * 2, grid.cellHeight * 2)
                fillMode: Image.PreserveAspectCrop
                autoTransform: true
                asynchronous: true
                clip: true
            }
            TapHandler { onTapped: win.showPhoto(cell.index) }
        }
    }
    Text {
        anchors.centerIn: parent
        visible: win.view === "grid" && photos.count === 0
        color: Theme.muted
        font { family: Theme.font; pixelSize: 16 }
        text: qsTr("No photos yet")
    }

    // the photos' bar: back to the camera, the grid or the photo, delete
    Row {
        visible: win.viewing
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 36 }
        spacing: 28
        Repeater {
            model: [
                {icon: "󰄀", act: "camera", show: true},
                {icon: win.view === "grid" ? "󰋩" : "󰕰", act: "toggle", show: photos.count > 0},
                {icon: "󰩹", act: "delete", show: win.view === "photo" && photos.count > 0},
            ]
            delegate: Rectangle {
                id: action
                required property var modelData
                visible: action.modelData.show
                width: 60; height: 60; radius: 30
                color: confirmDelete && action.modelData.act === "delete" ? Theme.red
                     : actionTap.pressed ? Theme.line : "#55ffffff"
                readonly property bool confirmDelete: win.confirmingDelete
                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font { family: Theme.font; pixelSize: 26 }
                    text: action.modelData.icon
                }
                TapHandler {
                    id: actionTap
                    onTapped: win.act(action.modelData.act)
                }
            }
        }
    }

    // delete takes a second tap within a few seconds (the photo goes to the trash)
    property bool confirmingDelete: false
    Timer { id: confirmTimeout; interval: 3000; onTriggered: win.confirmingDelete = false }
    function act(what) {
        if (what === "camera") {
            view = "finder"
        } else if (what === "toggle") {
            if (view === "grid")
                showPhoto(Math.max(0, gallery.currentIndex))
            else {
                grid.positionViewAtIndex(gallery.currentIndex, GridView.Contain)
                view = "grid"
            }
        } else if (what === "delete") {
            if (!confirmingDelete) {
                confirmingDelete = true
                confirmTimeout.restart()
                return
            }
            confirmingDelete = false
            const path = photos.get(gallery.currentIndex, "filePath")
            if (path && camera.deletePhoto(path))
                taken = taken.filter(p => p !== path)
            if (photos.count <= 1)          // that was the last one
                view = "finder"
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
}
