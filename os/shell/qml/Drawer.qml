pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The app drawer: the installed apps (the open ones are in the switcher, AppSwitcher.qml). It
// follows the finger from the bottom edge (Edge.qml drives it).
Sheet {
    id: drawer

    required property AppModel apps
    required property ToplevelModel toplevels
    required property Keyboard keyboard
    signal home()

    LayerShell.Window.scope: "mobarchy-drawer"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    onOpening: keyboard.visible = false
    // closed: the search is over too
    onShownChanged: if (!shown) {
        newWindow.visible = false
        endSearch()
    }

    // --- the search: the keyboard types into it (Keyboard.capturing), the grid follows ---
    readonly property bool searching: keyboard.capturing
    function startSearch() {
        keyboard.startCapture()
        keyboard.visible = true
    }
    function endSearch() {
        if (keyboard.capturing) {
            keyboard.stopCapture()
            keyboard.visible = false
        }
    }
    AppFilter {
        id: filtered
        source: drawer.apps
        query: drawer.keyboard.captured
    }
    Connections {
        target: drawer.keyboard
        // Return: the first match
        function onCaptureAccepted() {
            const id = filtered.first()
            if (id !== "")
                drawer.open_(id)
        }
    }
    function open_(appId) {
        if (!toplevels.activateApp(apps.windowIds(appId)))
            apps.launch(appId)
        close()
    }

    // Holding an app's icon: a new window of it, apart from the one it has open.
    function offerNewWindow(appId, name, tile) {
        const p = tile.mapToItem(panel, tile.width / 2, 0)
        newWindow.appId = appId
        newWindow.name = name
        newWindow.x = Math.max(12, Math.min(panel.width - newWindow.width - 12, p.x - newWindow.width / 2))
        newWindow.y = Math.max(12, p.y - newWindow.height - 6)
        newWindow.visible = true
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.4 * drawer.openness
    }

    Rectangle {
        id: panel
        width: parent.width
        height: parent.height
        y: (1 - drawer.openness) * height
        radius: 18
        color: Theme.bg
        border.color: Theme.line

        // Dragging the handle down closes the drawer.
        EdgeGesture {
            id: handle
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 40
            onBegan: drawer.beginDrag()
            onMoved: (distance) => drawer.dragTo(distance)
            onEnded: (velocity) => drawer.endDrag(velocity)
            onTapped: drawer.close()

            // (where the edge's bar is, and as it is: the same bar, picked up. The drawer
            // ends at the edge's top, the edge's exclusive zone)
            Rectangle {
                anchors { horizontalCenter: parent.horizontalCenter; verticalCenter: parent.top; verticalCenterOffset: 16 }
                width: 140; height: 5; radius: 2.5
                color: drawer.dragging || handle.pressed ? Theme.accent : Theme.muted
            }
        }

        // --- installed apps -------------------------------------------------------------
        Rectangle {
            id: search
            anchors { left: parent.left; right: parent.right; top: handle.bottom; leftMargin: 16; rightMargin: 16; topMargin: 4 }
            height: 42
            radius: 21
            color: Theme.panel
            border.color: drawer.searching ? Theme.accent : Theme.line
            // what is typed, the cursor after it; before anything is typed the cursor comes
            // first and the hint stays behind it, faint (nothing to rub out)
            Row {
                anchors { left: parent.left; right: clear.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                spacing: 0
                Text {
                    visible: !drawer.searching
                    text: "󰍉  "
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 15 }
                }
                Text {
                    text: drawer.keyboard.captured
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 15 }
                }
                Rectangle {
                    visible: drawer.searching
                    anchors.verticalCenter: parent.verticalCenter
                    width: 2; height: 20
                    color: Theme.accent
                    SequentialAnimation on opacity {
                        running: drawer.searching
                        loops: Animation.Infinite
                        NumberAnimation { to: 0; duration: 500; easing.type: Easing.InOutQuad }
                        NumberAnimation { to: 1; duration: 500; easing.type: Easing.InOutQuad }
                    }
                }
                Text {
                    visible: drawer.keyboard.captured === ""
                    leftPadding: drawer.searching ? 4 : 0
                    text: qsTr("Search apps")
                    color: Theme.muted
                    opacity: drawer.searching ? 0.6 : 1
                    font { family: Theme.font; pixelSize: 15 }
                }
            }
            Text {
                id: clear
                visible: drawer.searching
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: "󰅖"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 16 }
            }
            MouseArea {
                anchors.fill: parent
                onClicked: drawer.searching ? drawer.endSearch() : drawer.startSearch()
            }
        }
        Text {
            anchors { horizontalCenter: parent.horizontalCenter; top: search.bottom; topMargin: 40 }
            visible: drawer.searching && filtered.count === 0
            text: qsTr("No apps called “%1”").arg(drawer.keyboard.captured)
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
        GridView {
            maximumFlickVelocity: Theme.flickVelocity
            boundsBehavior: Theme.boundsBehavior
            flickDeceleration: Theme.flickDeceleration
            rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
            id: grid
            anchors {
                left: parent.left; right: parent.right; bottom: parent.bottom
                top: search.bottom
                topMargin: 8; leftMargin: 8; rightMargin: 8
            }
            clip: true
            readonly property int columns: Math.max(4, Math.floor(width / 120))
            cellWidth: width / columns
            cellHeight: 104
            model: filtered
            currentIndex: -1  // see Home.qml
            highlightFollowsCurrentItem: false
            // Pulling down past the top closes the drawer.
            onDragEnded: if (verticalOvershoot < -72) drawer.close()

            delegate: Item {
                id: tile
                required property string appId
                required property string name
                required property string icon
                width: grid.cellWidth; height: grid.cellHeight

                // A tap goes to the app (its window, if it has one open); holding it offers a
                // new window.
                MouseArea {
                    id: tap
                    anchors.fill: parent
                    onClicked: drawer.open_(tile.appId)
                    onPressAndHold: drawer.offerNewWindow(tile.appId, tile.name, tile)
                }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    spacing: 8
                    scale: tap.pressed ? 0.9 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 52; height: 52
                        sourceSize: Qt.size(104, 104)
                        asynchronous: true
                        source: "image://icon/" + tile.icon
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: tile.name
                        elide: Text.ElideRight
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
            }
        }
    
        // (offerNewWindow) A new window; for a web app, removing it too.
        Rectangle {
            id: newWindow
            property string appId
            property string name
            readonly property bool webApp: appId !== "" && drawer.apps.isWebApp(appId)
            visible: false
            z: 10
            width: newRow.implicitWidth + 36; height: 52; radius: 26
            color: newTap.pressed ? Theme.line : Theme.panel
            border.color: Theme.accent
            Row {
                id: newRow
                anchors.centerIn: parent
                spacing: 10
                Text { text: "󰖯"; color: Theme.accent; font { family: Theme.font; pixelSize: 18 }
                       anchors.verticalCenter: parent.verticalCenter }
                Text { text: qsTr("New window"); color: Theme.fg; font { family: Theme.font; pixelSize: 14 }
                       anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: newTap
                anchors.fill: parent
                onClicked: {
                    newWindow.visible = false
                    drawer.apps.launch(newWindow.appId)
                    drawer.close()
                }
            }
            Rectangle {
                visible: newWindow.webApp
                anchors { top: parent.bottom; topMargin: 8; horizontalCenter: parent.horizontalCenter }
                width: removeRow.implicitWidth + 36; height: 52; radius: 26
                color: removeTap.pressed ? Theme.line : Theme.panel
                border.color: Theme.red
                Row {
                    id: removeRow
                    anchors.centerIn: parent
                    spacing: 10
                    Text { text: "󰆴"; color: Theme.red; font { family: Theme.font; pixelSize: 18 }
                           anchors.verticalCenter: parent.verticalCenter }
                    Text { text: qsTr("Remove %1").arg(newWindow.name); color: Theme.fg
                           font { family: Theme.font; pixelSize: 14 }
                           anchors.verticalCenter: parent.verticalCenter }
                }
                MouseArea {
                    id: removeTap
                    anchors.fill: parent
                    onClicked: {
                        newWindow.visible = false
                        drawer.apps.removeWebApp(newWindow.appId)
                    }
                }
            }
        }
    }
}
