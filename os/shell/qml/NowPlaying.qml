import QtQuick
import Mobarchy.Shell

// What's playing, with its controls: in quick settings (full: cover, progress you can drag),
// on the lock screen (large: the same, bigger, the time left as phones show it), or compact.
// Acts on the player in charge (MediaPlayers).
Rectangle {
    id: card

    required property MediaPlayers player
    property bool compact: false
    property bool large: false

    visible: player.available && (player.title.length > 0 || player.identity.length > 0)
    implicitHeight: compact ? 72 : column.implicitHeight + 28
    radius: large ? 24 : 14
    color: large ? Qt.alpha(Theme.panel, 0.85) : Theme.panel
    border.color: Theme.line

    function time(ms) {
        const s = Math.floor(ms / 1000)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }

    component Control: Text {
        id: control
        signal clicked()
        property bool big: false
        property bool enabledControl: true
        color: enabledControl ? Theme.fg : Theme.muted
        opacity: enabledControl ? (controlTap.pressed ? 0.6 : 1) : 0.4
        font { family: Theme.font; pixelSize: big ? (card.large ? 44 : 30) : (card.large ? 32 : 22) }
        MouseArea {
            id: controlTap
            anchors { fill: parent; margins: -12 }
            enabled: control.enabledControl
            onClicked: control.clicked()
        }
    }

    Column {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: card.large ? 18 : 14 }
        spacing: card.large ? 16 : 12

        Row {
            width: parent.width
            spacing: 14

            Rectangle {
                id: cover
                width: card.compact ? 44 : card.large ? 72 : 64
                height: width
                radius: 10
                color: Theme.bg
                clip: true
                Image {
                    id: art
                    anchors.fill: parent
                    source: card.player.artUrl
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize: Qt.size(128, 128)
                    visible: status === Image.Ready
                }
                Text {
                    anchors.centerIn: parent
                    visible: !art.visible
                    text: "󰎆"
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: cover.width / 2 }
                }
            }

            Column {
                width: parent.width - cover.width - parent.spacing - (card.compact ? compactControls.width + 14 : 0)
                anchors.verticalCenter: cover.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: card.player.title || card.player.identity
                    elide: Text.ElideRight
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: card.large ? 17 : 14; bold: true }
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: card.player.artist || (card.player.title ? card.player.identity : "")
                    elide: Text.ElideRight
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: card.large ? 14 : 12 }
                }
            }

            Row {
                id: compactControls
                visible: card.compact
                anchors.verticalCenter: cover.verticalCenter
                spacing: 22
                Control {
                    text: card.player.playing ? "󰏤" : "󰐊"
                    enabledControl: card.player.canControl
                    onClicked: card.player.playPause()
                }
                Control {
                    text: "󰒭"
                    enabledControl: card.player.canGoNext
                    onClicked: card.player.next()
                }
            }
        }

        // Progress: drag to seek where the player allows it.
        Item {
            width: parent.width
            height: 26
            visible: !card.compact && card.player.length > 0
            Rectangle {
                id: track
                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 4 }
                height: 4
                radius: 2
                color: Theme.line
                Rectangle {
                    width: parent.width * Math.min(1, (seekArea.pressed ? seekArea.preview : card.player.position)
                                                       / Math.max(1, card.player.length))
                    height: parent.height
                    radius: 2
                    color: Theme.accent
                }
            }
            Text {
                anchors { left: parent.left; bottom: parent.bottom }
                text: card.time(seekArea.pressed ? seekArea.preview : card.player.position)
                color: Theme.muted
                font { family: Theme.font; pixelSize: card.large ? 12 : 10 }
            }
            Text {
                anchors { right: parent.right; bottom: parent.bottom }
                text: card.large ? "−" + card.time(Math.max(0, card.player.length - (seekArea.pressed ? seekArea.preview : card.player.position)))
                                 : card.time(card.player.length)
                color: Theme.muted
                font { family: Theme.font; pixelSize: card.large ? 12 : 10 }
            }
            MouseArea {
                id: seekArea
                property real preview: 0
                anchors { fill: track; topMargin: -12; bottomMargin: -12 }
                enabled: card.player.canSeek
                function at(x) { return Math.max(0, Math.min(1, x / width)) * card.player.length }
                onPressed: (m) => preview = at(m.x)
                onPositionChanged: (m) => preview = at(m.x)
                onReleased: card.player.seek(preview)
            }
        }

        Row {
            visible: !card.compact
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: card.large ? 64 : 44
            Control {
                anchors.verticalCenter: parent.verticalCenter
                text: "󰒮"
                enabledControl: card.player.canGoPrevious
                onClicked: card.player.previous()
            }
            Control {
                anchors.verticalCenter: parent.verticalCenter
                big: true
                text: card.player.playing ? "󰏤" : "󰐊"
                enabledControl: card.player.canControl
                onClicked: card.player.playPause()
            }
            Control {
                anchors.verticalCenter: parent.verticalCenter
                text: "󰒭"
                enabledControl: card.player.canGoNext
                onClicked: card.player.next()
            }
        }
    }
}
