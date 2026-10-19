import QtQuick
import QtQuick.Shapes
import Mobarchy.Shell

// Frigg on the home board: her orb, turning, breathing. Tapped, the conversation opens, live
// (ADR 0026), and the orb grows into the conversation's own.
BoardCard {
    id: card

    required property Talk talk

    readonly property color tint: talk.state === "speaking" ? Theme.green
                                : talk.state === "thinking" ? Theme.magenta : Theme.accent
    // wide: the orb at the left, words at the right; square or large: the orb alone, in the
    // middle of the card
    readonly property bool wide: cols > rows

    // Tapped: she gathers herself (a little smaller, spinning up), then the conversation opens
    // (her waking up begins at once).
    signal asked()
    onTapped: if (!windUp.running) {
        talk.start()
        orb.release(700, 0)
        windUp.start()
    }
    SequentialAnimation {
        id: windUp
        NumberAnimation { target: orb; property: "scale"; to: 0.86; duration: 170; easing.type: Easing.InOutQuad }
        NumberAnimation { target: orb; property: "scale"; to: 1.1; duration: 150; easing.type: Easing.OutQuad }
        ScriptAction { script: card.asked() }
        PauseAnimation { duration: 250 }
        NumberAnimation { target: orb; property: "scale"; to: 1; duration: 200 }
    }

    Item {
        id: stage
        // (square: the whole card, not its content's margins, for the middle)
        parent: card.wide ? card.contentItem : card
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom
                  right: card.wide ? undefined : parent.right
                  margins: card.wide ? 0 : 14 }
        width: card.wide ? height : undefined
        Shape {     // the glow
            anchors.centerIn: orb
            width: orb.width * 1.4; height: width
            opacity: 0.5
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: orb.width * 0.7; centerY: orb.width * 0.7; centerRadius: orb.width * 0.7
                    focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: Qt.alpha(card.tint, 0.45) }
                    GradientStop { position: 0.5; color: Qt.alpha(card.tint, 0.12) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathRectangle { width: orb.width * 1.4; height: orb.width * 1.4 }
            }
        }
        FriggOrb {
            id: orb
            anchors.centerIn: parent
            width: Math.min(parent.width, parent.height) * (card.wide ? 1.1 : 0.95)
            height: width
            points: card.rows >= 4 ? 380 : 280
            color: card.tint
            state: card.talk.state
            running: card.live && !card.editing
            Behavior on color { ColorAnimation { duration: 400 } }
        }
    }

    Column {
        visible: card.wide
        anchors { left: parent.left; leftMargin: card.wide ? stage.width + 14 : 0
                  right: parent.right; bottom: card.wide ? undefined : parent.bottom
                  verticalCenter: card.wide ? parent.verticalCenter : undefined }
        spacing: 2
        Text {
            width: parent.width
            horizontalAlignment: card.wide ? Text.AlignLeft : Text.AlignHCenter
            text: "FRIGG"
            color: card.tint
            font { family: Theme.font; pixelSize: 14; bold: true; letterSpacing: 3 }
        }
        Text {
            visible: card.wide || card.rows >= 4
            width: parent.width
            horizontalAlignment: card.wide ? Text.AlignLeft : Text.AlignHCenter
            elide: Text.ElideRight
            text: ({ listening: qsTr("Listening"), thinking: qsTr("Thinking …"), speaking: qsTr("Speaking"),
                     starting: qsTr("Waking up …") })[card.talk.state] ?? qsTr("Tap to talk")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12 }
        }
    }
}
