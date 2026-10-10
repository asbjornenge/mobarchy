pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The open apps, as cards with a picture of each window (WindowCapture), as on iOS and
// Android: the app in use in front, the ones used before it behind, to the left, a little
// over each other; swipe right through them, tap one to go to it, swipe one up to close it.
// "Close all" waits at the far end.
//
// From the bottom edge (Edge.qml): swipe up and hold. It comes up under the finger (the bar
// with it), the screen behind blurred (Hyprland's layer rule); back down cancels it, and let
// go it settles with the app in use in the middle. In a wide screen (the unfolded inner one)
// the cards are a grid.
LayerWindow {
    id: switcher

    required property ToplevelModel toplevels
    required property AppModel apps
    required property Haptics haptics
    property int insetBottom: 0

    shown: false
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-switcher"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    readonly property bool wide: width > height * 0.85
    // 0 = gone, 1 = up; while the finger holds it, it follows the finger
    property real openness: 0
    property bool following: false
    property real finger: 0       // px the finger is above the bottom edge

    // --- the gesture (Edge.qml) ----------------------------------------------------------
    function begin(distance) {
        settle.stop()
        capture.captureAll()
        following = true
        shown = true
        toFront()
        haptics.click()
        follow(distance)
    }
    function follow(distance) {
        finger = distance
        openness = Math.max(0, Math.min(1, (distance - 40) / 220))
    }
    // let go: up (or a flick up) stays, back down (or a flick down) cancels
    function release(velocity) {
        following = false
        if (velocity < -0.4 || (velocity < 0.4 && openness < 0.35))
            close()
        else
            open()
    }

    function open() {
        if (!shown) {
            capture.captureAll()
            shown = true
            toFront()
        }
        settle.to = 1
        settle.restart()
    }
    function close() {
        settle.to = 0
        settle.restart()
    }
    // the app in use in the middle (once the surface has its size)
    function toFront() {
        Qt.callLater(() => {
            row.currentIndex = 0
            row.positionViewAtIndex(0, ListView.Center)
        })
    }
    function go(index) {
        toplevels.activate(index)
        close()
    }

    NumberAnimation {
        id: settle
        target: switcher; property: "openness"; duration: 220; easing.type: Easing.OutCubic
        onFinished: if (switcher.openness === 0) switcher.shown = false
    }

    WindowCapture { id: capture }

    // The screen behind, blurred by Hyprland (layerrule: mobarchy-switcher, blur) through this
    // tint.
    Rectangle {
        anchors.fill: parent
        color: Theme.bgDark
        opacity: 0.55 * switcher.openness
        MouseArea { anchors.fill: parent; onClicked: switcher.close() }
    }

    Item {
        id: content
        anchors.fill: parent
        opacity: Math.min(1, switcher.openness * 1.6)
        // (from the app's own size down to the cards', as the finger goes up)
        scale: 1.25 - 0.25 * switcher.openness

        Text {
            anchors.centerIn: parent
            visible: switcher.toplevels.count === 0
            text: qsTr("No open apps")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 16 }
        }

        // The cards, newest on the right (on top), older ones to the left, each half over
        // the next. "Close all" after the oldest.
        ListView {
            id: row
            visible: !switcher.wide
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                      verticalCenterOffset: -20 }
            height: parent.height * 0.70
            orientation: ListView.Horizontal
            // Newest on the right: the view mirrored (and each card mirrored back). (A
            // right-to-left ListView doesn't keep its margins at the start.)
            transform: Scale { origin.x: row.width / 2; xScale: -1 }
            readonly property real cardWidth: switcher.width * 0.70
            spacing: -cardWidth * 0.22
            leftMargin: (width - cardWidth) / 2
            rightMargin: (width - cardWidth) / 2
            snapMode: ListView.SnapToItem
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: leftMargin
            preferredHighlightEnd: leftMargin + cardWidth
            maximumFlickVelocity: Theme.flickVelocity
            flickDeceleration: Theme.flickDeceleration
            cacheBuffer: cardWidth * 2
            model: switcher.toplevels
            delegate: Card {
                width: row.cardWidth
                height: row.height
                z: -index
                transform: Scale { origin.x: row.cardWidth / 2; xScale: -1 }
            }
            footer: Item {
                width: closeAll.width + 60 - row.spacing
                height: row.height
                z: -1000
                CloseAll {
                    id: closeAll
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: -row.spacing / 2
                    transform: Scale { origin.x: closeAll.width / 2; xScale: -1 }
                }
            }
        }
        GridView {
            id: grid
            visible: switcher.wide
            anchors { fill: parent; margins: 24; topMargin: 56; bottomMargin: 96 + switcher.insetBottom }
            readonly property int columns: Math.max(2, Math.floor(width / 420))
            cellWidth: width / columns
            cellHeight: cellWidth * Math.min(1.4, switcher.height / switcher.width) + 44
            model: switcher.toplevels
            delegate: Item {
                id: cell
                required property int index
                required property string title
                required property string appId
                required property bool activated
                width: grid.cellWidth; height: grid.cellHeight
                Card {
                    anchors { fill: parent; margins: 12 }
                    index: cell.index; title: cell.title; appId: cell.appId; activated: cell.activated
                }
            }
            footer: Item {
                width: grid.width; height: 96
                CloseAll { anchors.centerIn: parent }
            }
        }
    }

    // The bar, under the finger while it holds the switcher (Edge.qml's, picked up)
    Rectangle {
        visible: switcher.following
        anchors.horizontalCenter: parent.horizontalCenter
        y: switcher.height - switcher.insetBottom - 16 - switcher.finger
        width: 140; height: 5; radius: 2.5
        color: Theme.accent
    }

    component CloseAll: Rectangle {
        width: closeAllRow.implicitWidth + 36; height: 48; radius: 24
        color: closeAllTap.pressed ? Theme.line : Theme.panel
        border.color: Theme.line
        Row {
            id: closeAllRow
            anchors.centerIn: parent
            spacing: 10
            Text { text: "󰅙"; color: Theme.muted; font { family: Theme.font; pixelSize: 18 }
                   anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("Close all"); color: Theme.fg; font { family: Theme.font; pixelSize: 14 }
                   anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea {
            id: closeAllTap
            anchors.fill: parent
            onClicked: {
                for (let i = switcher.toplevels.count - 1; i >= 0; i--)
                    switcher.toplevels.close(i)
                switcher.close()
            }
        }
    }

    // One open window: its app above a picture of it. Tap: go to it; up: close it.
    component Card: Item {
        id: card
        required property int index
        required property string title
        required property string appId
        required property bool activated
        readonly property string picture: "image://windows/" + encodeURIComponent(appId) + "/"
                                          + encodeURIComponent(title) + "?" + capture.generation

        Item {
            id: body
            width: card.width; height: card.height
            opacity: 1 - Math.min(1, Math.max(0, -y / (card.height * 0.6)))

            Row {
                id: header
                anchors { left: parent.left; leftMargin: 4 }
                height: 36
                spacing: 10
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 26
                    sourceSize: Qt.size(52, 52)
                    asynchronous: true
                    source: "image://icon/" + switcher.apps.iconForAppId(card.appId)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: body.width * 0.55
                    text: switcher.apps.nameForAppId(card.appId) || card.title || card.appId
                    elide: Text.ElideRight
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 14; bold: true }
                }
            }
            Rectangle {
                id: frame
                anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 6 }
                radius: 22
                color: Theme.panel
                border.color: Theme.line
                // (rounded corners for the picture too: clip only cuts rectangles)
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    shadowEnabled: true
                    shadowBlur: 0.6
                    shadowOpacity: 0.5
                    maskSource: ShaderEffectSource {
                        sourceItem: Rectangle { width: frame.width; height: frame.height; radius: frame.radius }
                    }
                }
                Image {
                    id: shot
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    asynchronous: true
                    cache: false
                    source: card.picture
                }
                // no picture yet (or none to be had): the app's icon
                Image {
                    anchors.centerIn: parent
                    visible: shot.status !== Image.Ready || shot.implicitWidth <= 1
                    width: 96; height: 96
                    sourceSize: Qt.size(192, 192)
                    source: "image://icon/" + switcher.apps.iconForAppId(card.appId)
                }
            }

            DragHandler {
                target: body
                xAxis.enabled: false
                yAxis.maximum: 0
                onActiveChanged: {
                    if (active)
                        return
                    if (body.y < -card.height * 0.25)
                        throwAway.start()
                    else
                        back.start()
                }
            }
            TapHandler { onTapped: switcher.go(card.index) }
            NumberAnimation { id: back; target: body; property: "y"; to: 0; duration: 160; easing.type: Easing.OutCubic }
            SequentialAnimation {
                id: throwAway
                NumberAnimation { target: body; property: "y"; to: -card.height * 1.2; duration: 160 }
                ScriptAction {
                    script: {
                        switcher.toplevels.close(card.index)
                        body.y = 0
                    }
                }
            }
        }
    }
}
