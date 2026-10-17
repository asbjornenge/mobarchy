import QtQuick
import Mobarchy.Shell

// A widget on the home board: a flat card in the theme's colours, with a small title, that
// takes `cols` x `rows` of the board's square cells (2x2 small, 4x2 medium, 4x4 large).
// The board places it; a card with nothing to show sets `wanted` false and is left out.
Rectangle {
    id: card

    property int cols: 2
    property int rows: 2
    property bool wanted: true
    property string title
    property string icon
    // tapping the card (not its own controls)
    signal tapped()
    default property alias content: body.data

    radius: 24
    color: Qt.alpha(Theme.panel, 0.97)
    border.color: Qt.alpha(Theme.line, 0.8)
    border.width: 1
    scale: tap.pressed ? 0.97 : 1
    Behavior on scale { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }

    MouseArea {
        id: tap
        anchors.fill: parent
        onClicked: card.tapped()
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
    }
}
