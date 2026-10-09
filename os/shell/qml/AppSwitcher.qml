pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The open apps, as cards with a picture of each window (WindowCapture): swipe through them,
// tap one to go to it, swipe one up to close it. From the bottom edge: swipe up and hold
// (Edge.qml). In a tall screen the cards are a row to swipe through; in a wide one (the
// unfolded inner screen) a grid.
LayerWindow {
    id: switcher

    required property ToplevelModel toplevels
    required property AppModel apps
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
    property real openness: 0

    function open() {
        if (shown)
            return
        capture.captureAll()
        row.positionViewAtBeginning()
        shown = true
        fade.to = 1
        fade.restart()
    }
    function close() {
        fade.to = 0
        fade.restart()
    }
    function go(index) {
        toplevels.activate(index)
        close()
    }

    NumberAnimation {
        id: fade
        target: switcher; property: "openness"; duration: 180; easing.type: Easing.OutCubic
        onFinished: if (switcher.openness === 0) switcher.shown = false
    }

    WindowCapture { id: capture }

    Rectangle {
        anchors.fill: parent
        color: Theme.bgDark
        opacity: 0.97 * switcher.openness
        // a tap beside the cards: back to where it was
        MouseArea { anchors.fill: parent; onClicked: switcher.close() }
    }

    Item {
        anchors.fill: parent
        opacity: switcher.openness
        scale: 0.94 + 0.06 * switcher.openness

        Text {
            anchors.centerIn: parent
            visible: switcher.toplevels.count === 0
            text: qsTr("No open apps")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 16 }
        }

        // the cards: a row, or in a wide screen a grid
        ListView {
            id: row
            visible: !switcher.wide
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                      verticalCenterOffset: -24 }
            height: parent.height * 0.72
            orientation: ListView.Horizontal
            spacing: 22
            readonly property real cardWidth: switcher.width * 0.66
            leftMargin: (width - cardWidth) / 2
            rightMargin: (width - cardWidth) / 2
            snapMode: ListView.SnapToItem
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: leftMargin
            preferredHighlightEnd: leftMargin + cardWidth
            maximumFlickVelocity: Theme.flickVelocity
            model: switcher.toplevels
            delegate: Card {
                width: row.cardWidth
                height: row.height
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
        }

        // Close all
        Rectangle {
            visible: switcher.toplevels.count > 0
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom
                      bottomMargin: 40 + switcher.insetBottom }
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
                    width: body.width - 40
                    text: switcher.apps.nameForAppId(card.appId) || card.title || card.appId
                    elide: Text.ElideRight
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 14; bold: true }
                }
            }
            Rectangle {
                id: frame
                anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom; topMargin: 6 }
                radius: 18
                color: Theme.panel
                border.color: card.activated ? Theme.accent : Theme.line
                // (rounded corners for the picture too: clip only cuts rectangles)
                layer.enabled: true
                layer.effect: MultiEffect {
                    maskEnabled: true
                    maskSource: ShaderEffectSource {
                        sourceItem: Rectangle { width: frame.width; height: frame.height; radius: frame.radius }
                    }
                }
                Image {
                    id: shot
                    anchors.fill: parent
                    anchors.margins: 1
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
                id: swipe
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
