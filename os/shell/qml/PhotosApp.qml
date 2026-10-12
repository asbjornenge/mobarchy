pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Photos: the owner's pictures, ~/Pictures (PhotoLibrary): the camera's photos, screenshots,
// anything saved there; newest first, with the albums (folders) to pick from. A tap shows
// one (Gallery: swipe, zoom, delete). Its own window (mobarchy-shell --photos [path]), like
// the Clock; the agent shows a picture in it (org.mobarchy.Photos.Show).
Window {
    id: win

    visible: true
    title: qsTr("Photos")
    color: Theme.bg

    PhotoLibrary {
        id: library
        service: true
        onShowRequested: (path) => win.showPath(path)
    }

    function showPath(path) {
        if (path === "")
            return
        const row = library.indexOf(path)
        if (row >= 0)
            gallery.show(row)
        raise()
        requestActivate()
    }
    // mobarchy-shell --photos <path>
    Component.onCompleted: {
        const args = Qt.application.arguments
        const i = args.indexOf("--photos")
        if (i >= 0 && i + 1 < args.length)
            Qt.callLater(() => win.showPath(args[i + 1]))
    }

    readonly property var albumNames: ({ Camera: qsTr("Camera"), Screenshots: qsTr("Screenshots") })

    // the albums: all, then each folder
    Item {
        id: top
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 112
        visible: gallery.view === "grid"
        Text {
            anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 22 }
            text: qsTr("Photos")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 26; bold: true }
        }
        ListView {
            id: albums
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: 10
                      leftMargin: 16; rightMargin: 16 }
            height: 40
            orientation: ListView.Horizontal
            spacing: 8
            clip: true
            boundsBehavior: Theme.boundsBehavior
            model: [{name: "", count: -1}].concat(library.albums)
            delegate: Rectangle {
                id: chip
                required property var modelData
                readonly property bool on: library.album === modelData.name
                width: chipText.implicitWidth + 28
                height: 40
                radius: 20
                color: on ? Theme.accent : (chipTap.pressed ? Theme.line : Theme.panel)
                Text {
                    id: chipText
                    anchors.centerIn: parent
                    text: chip.modelData.name === "" ? qsTr("All")
                        : (win.albumNames[chip.modelData.name] ?? chip.modelData.name)
                    color: chip.on ? Theme.bg : Theme.fg
                    font { family: Theme.font; pixelSize: 14; bold: chip.on }
                }
                TapHandler { id: chipTap; onTapped: library.album = chip.modelData.name }
            }
        }
    }

    Gallery {
        id: gallery
        anchors.fill: parent
        model: library
        gridTop: top.height
        remove: (path) => library.trash(path)
    }
    Text {
        anchors.centerIn: parent
        visible: gallery.view === "grid" && library.count === 0
        width: parent.width - 48
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.muted
        font { family: Theme.font; pixelSize: 16 }
        text: qsTr("No pictures yet. Photos from the camera and screenshots (power and volume down) come here.")
    }
    // the time a picture is from, over it
    Text {
        visible: gallery.view === "photo" && gallery.currentPath !== ""
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 20 }
        readonly property date when: library.timeAt(gallery.current)
        text: when.toLocaleString(Qt.locale(), "d. MMMM yyyy  HH:mm")
        color: "white"
        style: Text.Outline
        styleColor: "#80000000"
        font { family: Theme.font; pixelSize: 14 }
    }
}
