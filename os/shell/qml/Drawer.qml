pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The app drawer: open apps on top, installed apps below. It follows the finger from the
// bottom edge (Edge.qml drives it).
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
    onShownChanged: if (!shown) newWindow.visible = false

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

            Rectangle {
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                width: 44; height: 5; radius: 2.5
                color: Theme.muted
            }
        }

        // --- open apps ------------------------------------------------------------------
        Text {
            id: openLabel
            anchors { left: parent.left; top: handle.bottom; leftMargin: 20 }
            text: qsTr("Open")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
        ListView {
            maximumFlickVelocity: Theme.flickVelocity
            flickDeceleration: Theme.flickDeceleration
            rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
            id: running
            anchors { left: parent.left; right: parent.right; top: openLabel.bottom; topMargin: 8 }
            height: 92
            leftMargin: 16; rightMargin: 16
            orientation: ListView.Horizontal
            spacing: 10
            model: drawer.toplevels
            currentIndex: -1  // see Home.qml
            highlightFollowsCurrentItem: false
            // Home (the agent) always comes first: back to it without closing anything.
            header: Item {
                width: homeCard.width + running.spacing
                height: 92
                Rectangle {
                    id: homeCard
                    width: 168; height: 92
                    radius: 14
                    color: Theme.panel
                    border.color: Theme.magenta
                    scale: homeTap.pressed ? 0.95 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    MouseArea { id: homeTap; anchors.fill: parent; onClicked: drawer.home() }
                    Text {
                        anchors { left: parent.left; top: parent.top; margins: 12 }
                        text: "✦"
                        color: Theme.magenta
                        font { family: Theme.font; pixelSize: 26 }
                    }
                    Text {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
                        text: "Mobarchy"
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
            }
            delegate: Rectangle {
                id: card
                required property int index
                required property string title
                required property string appId
                required property bool activated
                width: 168; height: 92
                radius: 14
                color: Theme.panel
                border.color: activated ? Theme.accent : Theme.line
                scale: cardTap.pressed ? 0.95 : 1
                Behavior on scale { NumberAnimation { duration: 90 } }

                MouseArea {
                    id: cardTap
                    anchors.fill: parent
                    onClicked: { drawer.toplevels.activate(card.index); drawer.close() }
                }
                Image {
                    id: cardIcon
                    anchors { left: parent.left; top: parent.top; margins: 12 }
                    width: 32; height: 32
                    sourceSize: Qt.size(64, 64)
                    asynchronous: true
                    source: "image://icon/" + drawer.apps.iconForAppId(card.appId)
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
                    text: card.title || drawer.apps.nameForAppId(card.appId)
                    elide: Text.ElideRight
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 12 }
                }
                MouseArea {
                    anchors { right: parent.right; top: parent.top }
                    width: 44; height: 44
                    onClicked: drawer.toplevels.close(card.index)
                    Text {
                        anchors.centerIn: parent
                        text: "󰅖"
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 16 }
                    }
                }
            }
        }

        // --- installed apps -------------------------------------------------------------
        GridView {
            maximumFlickVelocity: Theme.flickVelocity
            flickDeceleration: Theme.flickDeceleration
            rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
            id: grid
            anchors {
                left: parent.left; right: parent.right; bottom: parent.bottom
                top: running.bottom
                topMargin: 18; leftMargin: 8; rightMargin: 8
            }
            clip: true
            readonly property int columns: Math.max(4, Math.floor(width / 120))
            cellWidth: width / columns
            cellHeight: 104
            model: drawer.apps
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
                    onClicked: {
                        if (!drawer.toplevels.activateApp(drawer.apps.windowIds(tile.appId)))
                            drawer.apps.launch(tile.appId)
                        drawer.close()
                    }
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
