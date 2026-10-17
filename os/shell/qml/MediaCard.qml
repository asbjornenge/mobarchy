import QtQuick
import Mobarchy.Shell

// What's playing (MediaPlayers): the cover, the title and artist, how far it is, and the
// controls. Only while something plays or is paused; tapping it opens the player's app.
BoardCard {
    id: card

    required property MediaPlayers player
    required property AppModel apps

    wanted: player.available && (player.title.length > 0 || player.identity.length > 0)
    onTapped: if (player.desktopEntry) apps.launch(player.desktopEntry)

    // the position moves on between the player's own updates
    Timer {
        interval: 1000
        running: card.visible && card.player.playing && card.player.length > 0
        repeat: true
        onTriggered: card.player.refreshPosition()
    }

    Rectangle {
        id: cover
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: height
        radius: 16
        color: Theme.bg
        clip: true
        Text {
            anchors.centerIn: parent
            visible: art.status !== Image.Ready
            text: "󰝚"
            color: Theme.muted
            font { family: Theme.font; pixelSize: 34 }
        }
        Image {
            id: art
            anchors.fill: parent
            source: card.player.artUrl
            sourceSize { width: 256; height: 256 }
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: status === Image.Ready
        }
    }

    Item {
        anchors { left: cover.right; leftMargin: 16; right: parent.right; top: parent.top; bottom: parent.bottom }

        Column {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            spacing: 2
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: card.player.title || card.player.identity
                color: Theme.fg
                font { family: Theme.font; pixelSize: 16; bold: true }
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: card.player.artist || (card.player.title ? card.player.identity : "")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }

        // how far it is
        Rectangle {
            id: track
            visible: card.player.length > 0
            anchors { left: parent.left; right: parent.right; bottom: controls.top; bottomMargin: 10 }
            height: 4
            radius: 2
            color: Theme.line
            Rectangle {
                width: parent.width * Math.min(1, card.player.position / Math.max(1, card.player.length))
                height: parent.height
                radius: 2
                color: Theme.accent
            }
        }

        Row {
            id: controls
            anchors { left: parent.left; bottom: parent.bottom }
            spacing: 26
            component Control: Text {
                id: control
                signal clicked()
                property bool usable: true
                color: Theme.fg
                opacity: usable ? (controlTap.pressed ? 0.5 : 1) : 0.35
                font { family: Theme.font; pixelSize: 24 }
                MouseArea {
                    id: controlTap
                    anchors { fill: parent; margins: -12 }
                    enabled: control.usable
                    onClicked: control.clicked()
                }
            }
            Control { text: "󰒮"; usable: card.player.canGoPrevious; onClicked: card.player.previous() }
            Control { text: card.player.playing ? "󰏤" : "󰐊"; usable: card.player.canControl; onClicked: card.player.playPause() }
            Control { text: "󰒭"; usable: card.player.canGoNext; onClicked: card.player.next() }
        }
    }
}
