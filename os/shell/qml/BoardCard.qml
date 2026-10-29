import QtQuick
import Mobarchy.Shell

// A widget on the home board: a flat card in the theme's colours, with a small title, that
// takes `cols` x `rows` of the board's square cells (2x1 compact, 2x2 small, 4x2 medium,
// 4x4 large). A compact card has no title: what it shows says what it is.
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
    readonly property bool compact: rows === 1
    // what a compact card shows instead of its content: an icon and a value on a line, and a
    // caption under them
    property string compactIcon: icon
    property color compactIconColor: Theme.accent
    property string compactValue
    property string compactCaption
    // drawn instead of the icon (a battery that shows its charge)
    property Component compactGraphic: null
    // compact, but the content after all (a card that needs to ask for something)
    property bool contentWhenCompact: false
    readonly property bool showsContent: !compact || contentWhenCompact
    property string title
    property string icon
    // tapping the card (not its own controls)
    signal tapped()
    // holding it edits the board; a card with a use of its own for a hold says so (it can be
    // moved still, from the board's editing started on another card)
    property bool holdToEdit: true
    signal longPressed()
    // an app, to the front if it is open, else started (the board's: one window per app)
    property var openApp: null
    default property alias content: body.data
    readonly property alias contentItem: body

    // the board's editing
    property bool editing: false
    // in sight now (the board shown, its window in front): what moves, moves only then
    property bool live: true
    property bool placed: false     // the board has put it somewhere (from then on it glides)
    readonly property bool dragged: drag.active
    signal held()
    signal removeAsked()
    signal resizeAsked()
    signal dropped()

    // as the theme has them: filled with the panel colour, or a thin line only (ADR 0025)
    readonly property bool outlined: Theme.cards === "outline"
    radius: 24
    color: outlined ? "transparent" : Qt.alpha(Theme.panel, 0.97)
    border.color: outlined ? Qt.alpha(Theme.fg, 0.16) : Qt.alpha(Theme.line, 0.8)
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
        onPressAndHold: card.holdToEdit ? card.held() : card.longPressed()
    }

    Row {
        id: head
        visible: card.title !== "" && !card.compact
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
        anchors { fill: parent; leftMargin: card.compact ? 16 : 18; rightMargin: card.compact ? 16 : 18
                  bottomMargin: card.compact ? 12 : 16
                  topMargin: card.compact ? 12 : card.title !== "" ? 40 : 16 }
        visible: card.showsContent
        opacity: card.editing ? 0.35 : 1
    }
    Column {
        visible: !card.showsContent
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                  leftMargin: 16; rightMargin: 16 }
        opacity: card.editing ? 0.35 : 1
        spacing: 1
        Row {
            spacing: 8
            Loader {
                anchors.verticalCenter: parent.verticalCenter
                active: card.compactGraphic !== null
                visible: active
                sourceComponent: card.compactGraphic
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: card.compactIcon !== ""
                text: card.compactIcon
                color: card.compactIconColor
                font { family: Theme.font; pixelSize: 20 }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: card.compactValue
                color: Theme.fg
                font { family: Theme.font; pixelSize: 24; weight: Font.Light }
            }
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            text: card.compactCaption
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
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
