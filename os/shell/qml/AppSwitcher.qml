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
// with it), the screen behind blurred (a picture of it, taken as the finger touched the edge,
// blurred once: a compositor's blur is worked out again for every frame, and halved the frame
// rate as the cards moved); back down cancels it, and let
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
    // While the finger holds it, the cards slide in from beyond the left edge, the first only
    // a little way in; let go, they glide into place, the first in the middle.
    // (how far off to the left, 0..1, 1 out of sight: a fraction, as the window may not have
    // its width yet when the gesture begins)
    property real away: 0
    readonly property real peek: width
    // (while the finger holds it, it eases after the finger, so it slides in rather than
    // jumping to where the finger already is)
    Behavior on away {
        enabled: switcher.following
        NumberAnimation { duration: 260; easing.type: Easing.OutCubic }
    }
    Behavior on openness {
        enabled: switcher.following
        NumberAnimation { duration: 200; easing.type: Easing.OutCubic }
    }

    // --- the gesture (Edge.qml) ----------------------------------------------------------
    // A finger on the edge: the screen as it is, before anything comes up over it.
    function prepare(window) {
        capture.captureScreen(window)
    }
    function begin(distance) {
        settle.stop()
        glide.stop()
        capture.captureAll()
        openness = 0
        away = 1
        shown = true
        following = true
        toFront()
        haptics.tick()
        follow(distance)
    }
    function follow(distance) {
        finger = distance
        const o = Math.max(0, Math.min(1, (distance - 40) / 220))
        openness = o
        away = 1 - 0.5 * o
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
        glide.to = 0
        glide.restart()
    }
    function close() {
        settle.to = 0
        settle.restart()
        if (following || away !== 0) {
            glide.to = 1
            glide.restart()
        }
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
        id: glide
        target: switcher; property: "away"; duration: 320; easing.type: Easing.OutCubic
    }
    NumberAnimation {
        id: settle
        target: switcher; property: "openness"; duration: 220; easing.type: Easing.OutCubic
        onFinished: if (switcher.openness === 0) switcher.shown = false
    }

    WindowCapture { id: capture }

    // The screen behind, blurred, through a tint
    Item {
        anchors.fill: parent
        opacity: Math.min(1, switcher.openness * 2)
        // (blurred once, into a texture of its own: then each frame only draws it)
        layer.enabled: true
        Image {
            id: screenShot
            anchors.fill: parent
            visible: false
            fillMode: Image.PreserveAspectCrop
            asynchronous: false
            cache: false
            source: capture.screenGeneration > 0 ? "image://windows/screen?" + capture.screenGeneration : ""
        }
        MultiEffect {
            anchors.fill: parent
            source: screenShot
            visible: screenShot.status === Image.Ready
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 48
            saturation: 0.15
        }
    }
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

        // The cards, newest on the right (on top), the one in use a little right of the
        // middle, older ones to the left, stacked closer the older they are (a few in
        // sight). "Close all" after the oldest.
        ListView {
            id: row
            visible: !switcher.wide
            // (anchored on the left with a margin, so the view itself moves)
            anchors { left: parent.left; verticalCenter: parent.verticalCenter
                      verticalCenterOffset: -20; leftMargin: -switcher.away * switcher.peek }
            width: parent.width
            height: parent.height * 0.70
            orientation: ListView.Horizontal
            // Newest on the right: the view mirrored (and each card mirrored back). (A
            // right-to-left ListView doesn't keep its margins at the start.)
            transform: Scale { origin.x: row.width / 2; xScale: -1 }
            readonly property real cardWidth: switcher.width * 0.62
            spacing: -cardWidth * 0.25
            readonly property real step: cardWidth + spacing
            // Where a card's left edge is on the screen, q steps to the left of the middle
            // (negative: to the right). The view lays them out evenly; each is moved there.
            readonly property real middleLeft: width * 0.29
            function leftEdge(q) {
                if (q <= 0)
                    return middleLeft - q * width * 0.60   // the next: just in sight on the right
                if (q <= 1)
                    return middleLeft + (width * 0.12 - middleLeft) * q
                if (q <= 2)
                    return width * (0.12 - 0.08 * (q - 1))
                return width * (0.04 - 0.04 * (q - 2))     // then under the one before
            }
            leftMargin: (width - cardWidth) / 2
            rightMargin: (width - cardWidth) / 2
            snapMode: ListView.SnapToItem
            highlightRangeMode: ListView.StrictlyEnforceRange
            preferredHighlightBegin: leftMargin
            preferredHighlightEnd: leftMargin + cardWidth
            maximumFlickVelocity: Theme.flickVelocity
            boundsBehavior: Theme.boundsBehavior
            rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
            flickDeceleration: Theme.flickDeceleration
            cacheBuffer: cardWidth * 2
            // (the stacked ones are outside where the view would lay them: shown all the same)
            displayMarginBeginning: width
            displayMarginEnd: width
            model: switcher.toplevels
            delegate: Card {
                width: row.cardWidth
                height: row.height
                z: -index
                slides: true
                transform: Scale { origin.x: row.cardWidth / 2; xScale: -1 }
                // How far the card is to the left of the middle, in steps (the view is
                // mirrored: its x runs right to left on the screen).
                readonly property real q: (x + width / 2 - row.contentX - row.width / 2) / row.step
                offset: row.leftEdge(q) - (row.width / 2 - q * row.step - width / 2)
                depth: q
            }
            // "Close all": to the left of the oldest, once that one is in the middle
            footer: Item {
                id: footer
                width: 1
                height: row.height
                z: -1000
                // (as the cards: steps to the left of the middle, and the screen's x)
                readonly property real q: (x - row.contentX - row.width / 2) / row.step
                readonly property real seen: row.width / 2 - q * row.step
                readonly property real wanted: row.middleLeft / 2 - Math.max(0, q - 1) * row.width * 0.5
                opacity: Math.max(0, Math.min(1, (1.8 - q) / 0.8))
                transform: Translate { x: footer.seen - footer.wanted }
                CloseAllRound {
                    id: closeRound
                    anchors.centerIn: parent
                    transform: Scale { origin.x: closeRound.width / 2; xScale: -1 }
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
        y: switcher.height - switcher.insetBottom - 16 - switcher.finger - height / 2
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

    // (the same, small: in the strip left of the cards)
    component CloseAllRound: Column {
        spacing: 6
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 52; height: 52; radius: 26
            color: roundTap.pressed ? Theme.line : Theme.panel
            border.color: Theme.line
            Text {
                anchors.centerIn: parent
                text: "󰅙"; color: Theme.fg
                font { family: Theme.font; pixelSize: 22 }
            }
            MouseArea {
                id: roundTap
                anchors { fill: parent; margins: -8 }
                onClicked: {
                    for (let i = switcher.toplevels.count - 1; i >= 0; i--)
                        switcher.toplevels.close(i)
                    switcher.close()
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Close all"); color: Theme.muted
            font { family: Theme.font; pixelSize: 11 }
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

        // the apps before the one in use come in from the left as the switcher comes up
        property bool slides: false
        // (px to the right, on the screen: the row's spread; and how far under the middle
        // one it is, in steps: its name goes, then its icon, where the next one is too close)
        property real offset: 0
        property real depth: 0
        Item {
            id: body
            width: card.width; height: card.height
            x: card.offset + (card.slides && card.index > 0 ? -(1 - switcher.openness) * switcher.width * 0.8 : 0)
            opacity: 1 - Math.min(1, Math.max(0, -y / (card.height * 0.6)))

            Row {
                id: header
                anchors { left: parent.left; leftMargin: 4 }
                height: 36
                spacing: 10
                Image {
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 1 - Math.max(0, Math.min(1, card.depth - 1.8))
                    width: 26; height: 26
                    sourceSize: Qt.size(52, 52)
                    asynchronous: true
                    source: "image://icon/" + switcher.apps.iconForAppId(card.appId)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    opacity: 1 - Math.max(0, Math.min(1, (card.depth - 0.3) / 0.4))
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
                    // (in memory already: at once, so a newer picture replaces the last one
                    // without a blank in between)
                    asynchronous: false
                    cache: false
                    source: card.picture
                }
                // no pictures to be had (a compositor without the protocols): the app's icon
                Image {
                    anchors.centerIn: parent
                    visible: !capture.supported
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
