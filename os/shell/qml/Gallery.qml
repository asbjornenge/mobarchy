pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtMultimedia
import Mobarchy.Shell

// Pictures as on a phone, for the Photos app and the camera: a grid of them, newest first;
// a tap shows one on its own, the whole screen; swipe for the next, pinch or double-tap to
// zoom (then drag to look around); a video plays when tapped. Delete takes a second tap.
//
// model: anything with a filePath role (PhotoLibrary, a FolderListModel), newest first.
// remove: function(path) -> bool, what deleting does (to the trash).
Item {
    id: gallery

    required property var model
    property var remove: null
    // the grid's room at the top (the app's albums) and bottom (the camera's buttons)
    property real gridTop: 0
    property real gridBottom: 0
    // what shows: the grid, or one picture
    property string view: "grid"        // grid, photo
    readonly property int current: viewer.currentIndex
    readonly property string currentPath: view === "photo" && viewer.currentItem ? viewer.currentItem.filePath : ""
    signal backFromPhoto()              // the photo's back: to the grid, unless the owner handles it

    function isVideo(path: string): bool { return /\.(mp4|webm|mkv)$/i.test(path) }
    // a video's thumbnail (the camera source makes one: the freedesktop thumbnail spec)
    function thumbnail(path: string): string {
        return StandardPaths.writableLocation(StandardPaths.GenericCacheLocation)
               + "/thumbnails/large/" + Qt.md5("file://" + path) + ".png"
    }
    function show(index) {
        viewer.positionViewAtIndex(index, ListView.Beginning)
        viewer.currentIndex = index
        view = "photo"
    }
    function showGrid() {
        if (viewer.currentIndex >= 0)
            grid.positionViewAtIndex(viewer.currentIndex, GridView.Contain)
        view = "grid"
    }

    // --- all of them -------------------------------------------------------------------------
    GridView {
        id: grid
        anchors { fill: parent; topMargin: gallery.gridTop; bottomMargin: gallery.gridBottom; leftMargin: 2; rightMargin: 2 }
        visible: gallery.view === "grid"
        clip: true
        readonly property int columns: Math.max(3, Math.floor(width / 130))
        cellWidth: width / columns
        cellHeight: cellWidth
        model: gallery.model
        boundsBehavior: Theme.boundsBehavior
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
        delegate: Item {
            id: cell
            required property string filePath
            required property int index
            readonly property bool video: gallery.isVideo(filePath)
            width: grid.cellWidth
            height: grid.cellHeight
            Rectangle {
                anchors { fill: parent; margins: 1 }
                color: Theme.panel
            }
            Image {
                anchors { fill: parent; margins: 1 }
                source: cell.video ? "file://" + gallery.thumbnail(cell.filePath) : "file://" + cell.filePath
                sourceSize: Qt.size(grid.cellWidth * 2, grid.cellHeight * 2)
                fillMode: Image.PreserveAspectCrop
                autoTransform: true
                asynchronous: true
                clip: true
            }
            Text {
                anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
                visible: cell.video
                color: "white"
                style: Text.Outline
                font { family: Theme.font; pixelSize: 22 }
                text: "󰐊"
            }
            TapHandler { onTapped: gallery.show(cell.index) }
        }
    }

    // --- one at a time -------------------------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        visible: gallery.view === "photo"
        color: "black"
    }
    ListView {
        id: viewer
        anchors.fill: parent
        visible: gallery.view === "photo"
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        boundsBehavior: Flickable.StopAtBounds
        // (zoomed in, a drag looks around the picture instead)
        interactive: !(currentItem && currentItem.zoomed)
        cacheBuffer: width
        model: gallery.model
        delegate: Item {
            id: page
            required property string filePath
            required property int index
            readonly property bool video: gallery.isVideo(filePath)
            readonly property bool current: ListView.isCurrentItem && gallery.view === "photo"
            readonly property bool zoomed: picture.scale > 1.01
            width: viewer.width
            height: viewer.height
            clip: true
            onCurrentChanged: if (!current) page.unzoom()
            function unzoom() {
                picture.scale = 1
                picture.x = 0
                picture.y = 0
            }

            Image {
                id: picture
                width: page.width
                height: page.height
                // a video: its thumbnail until it plays
                source: page.video ? "file://" + gallery.thumbnail(page.filePath) : "file://" + page.filePath
                // (sharp when zoomed in too)
                sourceSize: Qt.size(page.width * 3, page.height * 3)
                fillMode: Image.PreserveAspectFit
                autoTransform: true
                asynchronous: true
                Behavior on scale { enabled: !pinch.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on x { enabled: !pinch.active && !pan.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
                Behavior on y { enabled: !pinch.active && !pan.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            }
            PinchHandler {
                id: pinch
                target: picture
                enabled: !page.video
                scaleAxis.minimum: 1
                scaleAxis.maximum: 5
                rotationAxis.enabled: false
                onActiveChanged: if (!active && picture.scale < 1.05) page.unzoom()
            }
            DragHandler {
                id: pan
                target: picture
                enabled: page.zoomed
            }
            TapHandler {
                enabled: !page.video
                onDoubleTapped: (point) => {
                    if (page.zoomed) {
                        page.unzoom()
                    } else {
                        // in on where the finger is
                        const s = 2.5
                        picture.x = (page.width / 2 - point.position.x) * (s - 1)
                        picture.y = (page.height / 2 - point.position.y) * (s - 1)
                        picture.scale = s
                    }
                }
            }

            // a video: plays when tapped, stops when swiped away
            Loader {
                anchors.fill: parent
                active: page.video && page.current
                sourceComponent: Item {
                    MediaPlayer {
                        id: player
                        source: "file://" + page.filePath
                        videoOutput: videoOut
                        audioOutput: AudioOutput {}
                    }
                    VideoOutput {
                        id: videoOut
                        anchors.fill: parent
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: player.playbackState !== MediaPlayer.PlayingState
                        color: "white"
                        font { family: Theme.font; pixelSize: 64 }
                        text: "󰐊"
                    }
                    TapHandler {
                        onTapped: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                    }
                }
            }
        }
    }

    // the photo's bar: back to the grid, delete
    Row {
        visible: gallery.view === "photo"
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 36 }
        spacing: 28
        Repeater {
            model: [
                {icon: "󰕰", act: "back", show: true},
                {icon: "󰩹", act: "delete", show: gallery.remove !== null},
            ]
            delegate: Rectangle {
                id: action
                required property var modelData
                visible: action.modelData.show
                width: 60; height: 60; radius: 30
                color: gallery.confirmingDelete && action.modelData.act === "delete" ? Theme.red
                     : actionTap.pressed ? Theme.line : "#55ffffff"
                Text {
                    anchors.centerIn: parent
                    color: "white"
                    font { family: Theme.font; pixelSize: 26 }
                    text: action.modelData.icon
                }
                TapHandler {
                    id: actionTap
                    onTapped: gallery.act(action.modelData.act)
                }
            }
        }
    }

    // delete takes a second tap within a few seconds (it goes to the trash)
    property bool confirmingDelete: false
    Timer { id: confirmTimeout; interval: 3000; onTriggered: gallery.confirmingDelete = false }
    function act(what) {
        if (what === "back") {
            confirmingDelete = false
            gallery.backFromPhoto()
            showGrid()
        } else if (what === "delete") {
            if (!confirmingDelete) {
                confirmingDelete = true
                confirmTimeout.restart()
                return
            }
            confirmingDelete = false
            const path = currentPath
            const index = viewer.currentIndex
            if (path && remove && remove(path)) {
                if (viewer.count === 0)
                    view = "grid"
                else
                    Qt.callLater(() => show(Math.min(index, viewer.count - 1)))
            }
        }
    }
}
