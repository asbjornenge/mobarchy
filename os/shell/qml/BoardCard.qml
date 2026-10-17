import QtQuick
import Mobarchy.Shell

// A widget on the home board: a flat card in the theme's colours, with a small title, that
// takes `cols` x `rows` of the board's square cells (2x2 small, 4x2 medium, 4x4 large).
// The board places it; a card with nothing to show sets `wanted` false and is left out.
// Held, the board goes into editing: the card can then be dragged, resized and removed.
Rectangle {
    id: card

    // what it is, in home.json's "board" ("weather"), and its name in the board's editor
    property string kind
    property string name
    // the sizes it comes in, [cols, rows], the first its default
    property var sizes: [[2, 2]]
    property int cols: sizes[0][0]
    property int rows: sizes[0][1]
    property bool wanted: true
    property string title
    property string icon
    // tapping the card (not its own controls)
    signal tapped()
    default property alias content: body.data

    // the board's editing
    property bool editing: false
    property bool placed: false     // the board has put it somewhere (from then on it glides)
    readonly property bool dragged: drag.active
    signal held()
    signal removeAsked()
    signal resizeAsked()
    signal dropped()

    radius: 24
    color: Qt.alpha(Theme.panel, 0.97)
    border.color: Qt.alpha(Theme.line, 0.8)
    border.width: 1
    scale: drag.active ? 1.04 : tap.pressed && !editing ? 0.97 : 1
    z: drag.active ? 10 : 0
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
    // placed by the board: glides there (not while a finger has it)
    Behavior on x { enabled: card.placed && !drag.active; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on y { enabled: card.placed && !drag.active; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on width { enabled: card.placed; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    Behavior on height { enabled: card.placed; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    MouseArea {
        id: tap
        anchors.fill: parent
        onClicked: if (!card.editing) card.tapped()
        onPressAndHold: card.held()
    }

    Row {
        id: head
        visible: card.title !== ""
        anchors { left: parent.left; top: parent.top; leftMargin: 18; topMargin: 16 }
        spacing: 7
        Text {
            anchors.verticalCenter: parent.verticalCenter
            visible: card.icon !== ""
            text: card.icon
            color: Theme.accent
            font { family: Theme.font; pixelSize: 13 }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: card.title.toUpperCase()
            color: Theme.muted
            font { family: Theme.font; pixelSize: 11; bold: true; letterSpacing: 1.2 }
        }
    }

    Item {
        id: body
        anchors { fill: parent; leftMargin: 18; rightMargin: 18; bottomMargin: 16
                  topMargin: card.title !== "" ? 40 : 16 }
        opacity: card.editing ? 0.35 : 1
    }

    // --- editing: over the content, which takes no touches then ----------------------------
    Item {
        anchors.fill: parent
        visible: card.editing
        z: 20
        MouseArea { anchors.fill: parent; onPressAndHold: {} }   // the content's controls rest
        DragHandler {
            id: drag
            target: card
            onActiveChanged: if (!active) card.dropped()
        }
        Text {
            anchors.centerIn: parent
            width: parent.width - 24
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: card.name
            color: Theme.fg
            font { family: Theme.font; pixelSize: 15; bold: true }
        }
        component EditButton: Rectangle {
            id: button
            property string glyph
            signal clicked()
            width: 34; height: 34; radius: 17
            color: buttonTap.pressed ? Theme.line : Theme.bg
            border.color: Theme.line
            Text {
                anchors.centerIn: parent
                text: button.glyph
                color: Theme.fg
                font { family: Theme.font; pixelSize: 16 }
            }
            MouseArea {
                id: buttonTap
                anchors { fill: parent; margins: -8 }
                onClicked: button.clicked()
            }
        }
        EditButton {
            anchors { right: parent.right; top: parent.top; margins: 8 }
            glyph: "󰅖"
            onClicked: card.removeAsked()
        }
        EditButton {
            visible: card.sizes.length > 1
            anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
            glyph: "󰩨"
            onClicked: card.resizeAsked()
        }
    }
}
