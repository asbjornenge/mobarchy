pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import Mobarchy.Shell

// Frigg live (ADR 0026): her orb for what she is doing (listening, thinking, speaking), what
// was heard and what she says; at the top (on an unlocked phone) whether "Hei Frigg" starts a
// conversation, and learning the owner's voice. Used by the conversation at home and by the
// overlay over other apps.
//
// Small (`small`: the conversation in text), the orb alone, in the top right corner; a tap
// there asks for her live again. Large, a tap interrupts her
// (or, between conversations, starts one) and a finger turns her.
Item {
    id: stage

    required property Talk talk
    required property FriggSettings settings
    property bool locked: false
    property bool small: false
    property bool shown: true          // her orb grows in (and turns) when it comes
    property real topInset: 64         // room kept at the top (the status bar, a header)
    property real bottomInset: 0
    property bool offerText: true      // the "Aa" button: on to text
    // the orb's place when small: its size, and its right and top edges in the stage
    property real smallSize: 40
    property real smallRight: 16
    property real smallTop: 0

    signal closeAsked()
    signal textAsked()
    signal liveAsked()

    readonly property color tint: talk.state === "speaking" ? Theme.green
                                : talk.state === "thinking" ? Theme.magenta : Theme.accent

    // "Hei Frigg": always · while charging · off
    Row {
        id: wakeRow
        visible: !stage.locked && !stage.talk.enrolling && !stage.small
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: stage.topInset }
        spacing: 6
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("“Hei Frigg”")
            color: Theme.muted
            rightPadding: 6
            font { family: Theme.font; pixelSize: 14 }
        }
        Repeater {
            model: [{ id: "always", label: qsTr("Always") }, { id: "charging", label: qsTr("Charging") },
                    { id: "off", label: qsTr("Off") }]
            delegate: Rectangle {
                id: choice
                required property var modelData
                readonly property bool chosen: stage.settings.wake === modelData.id
                width: label.implicitWidth + 24
                height: 34
                radius: 17
                color: chosen ? Theme.accent : "transparent"
                border { color: chosen ? Theme.accent : Theme.muted; width: 1 }
                Text {
                    id: label
                    anchors.centerIn: parent
                    text: choice.modelData.label
                    color: choice.chosen ? Theme.bg : Theme.fg
                    font { family: Theme.font; pixelSize: 14 }
                }
                MouseArea {
                    anchors { fill: parent; margins: -4 }
                    onClicked: stage.settings.wake = choice.modelData.id
                }
            }
        }
    }

    // Voice Match: learn the owner's voice
    Rectangle {
        visible: wakeRow.visible
        anchors { horizontalCenter: parent.horizontalCenter; top: wakeRow.bottom; topMargin: 14 }
        width: learn.implicitWidth + 28
        height: 34
        radius: 17
        color: "transparent"
        border { color: Theme.muted; width: 1 }
        Text {
            id: learn
            anchors.centerIn: parent
            text: qsTr("󰍬  Learn my voice")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 14 }
        }
        MouseArea {
            anchors { fill: parent; margins: -4 }
            onClicked: stage.talk.enroll()
        }
    }

    // While learning the voice: the sentence to read
    Column {
        visible: stage.talk.enrolling && stage.talk.enrollText !== "" && !stage.small
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28
                  topMargin: stage.topInset + 56 }
        spacing: 16
        Text {
            text: qsTr("Read aloud  ·  %1 of %2").arg(stage.talk.enrollIndex + 1).arg(stage.talk.enrollCount)
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
        Text {
            width: parent.width
            text: stage.talk.enrollText
            color: Theme.fg
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 26 }
        }
    }

    // where the orb is large: the room the words leave
    Item {
        id: room
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: talkColumn.top
                  topMargin: stage.topInset + (wakeRow.visible ? 106 : 0); bottomMargin: 8 }
    }
    readonly property real largeSize: Math.min(room.width - 40, room.height)

    // The orb: it grows in, and moves between the middle and the corner.
    Item {
        id: holder
        visible: !stage.talk.enrolling || stage.small
        // 0 large in the middle, 1 small in the corner: it glides between them
        property real t: 0
        Component.onCompleted: t = stage.small ? 1 : 0
        NumberAnimation { id: glide; target: holder; property: "t"; duration: 340; easing.type: Easing.InOutCubic }
        Connections {
            target: stage
            function onSmallChanged() {
                glide.stop()
                glide.from = holder.t
                glide.to = stage.small ? 1 : 0
                glide.start()
            }
        }
        width: stage.largeSize + (stage.smallSize - stage.largeSize) * t
        height: width
        x: (room.x + (room.width - stage.largeSize) / 2) * (1 - t) + (stage.width - stage.smallRight - stage.smallSize) * t
        y: (room.y + (room.height - stage.largeSize) / 2) * (1 - t) + stage.smallTop * t

        Shape {     // the glow
            anchors.centerIn: parent
            width: holder.width * 1.5; height: width
            opacity: 0.55
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: holder.width * 0.75; centerY: holder.width * 0.75; centerRadius: holder.width * 0.75
                    focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: Qt.alpha(stage.tint, 0.5) }
                    GradientStop { position: 0.45; color: Qt.alpha(stage.tint, 0.15) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathRectangle { width: holder.width * 1.5; height: holder.width * 1.5 }
            }
        }
        FriggOrb {
            id: orb
            anchors.fill: parent
            // (fewer once it is small: changing them makes it anew, so not on the way)
            points: holder.width < 100 ? 160 : 440
            calm: holder.width < 100 ? 0 : 0.8   // (large, the same breathing is more: slower and less)
            color: stage.tint
            state: stage.talk.state
            running: stage.visible
            Behavior on color { ColorAnimation { duration: 400 } }
            // growing in, with a turn
            scale: stage.shown ? 1 : 0.35
            Behavior on scale { NumberAnimation { duration: 520; easing.type: Easing.OutBack } }
            opacity: stage.shown ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 300 } }

        }
        TapHandler {
            onTapped: stage.small ? stage.liveAsked()
                      : stage.talk.talking ? stage.talk.interrupt() : stage.talk.start()
        }
        // a finger turns her, and a flick spins her on
        DragHandler {
            enabled: !stage.small
            target: null
            property point last
            onActiveChanged: if (active) last = Qt.point(0, 0)
                             else orb.release(centroid.velocity.x, centroid.velocity.y)
            onTranslationChanged: {
                orb.turnBy(translation.x - last.x, translation.y - last.y)
                last = Qt.point(translation.x, translation.y)
            }
        }
    }

    Column {
        id: talkColumn
        visible: !stage.small
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  margins: 28; bottomMargin: 48 + stage.bottomInset }
        spacing: 22

        // what Frigg says, and what was heard
        Text {
            width: parent.width
            visible: text !== ""
            text: stage.talk.said
            color: Theme.fg
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 20 }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: stage.talk.heard ? "«" + stage.talk.heard + "»"
                  + (stage.talk.guest ? "  ·  " + qsTr("not the owner's voice") : "") : ""
            color: Theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 15; italic: true }
        }
        Text {
            width: parent.width
            visible: stage.talk.error !== "" || stage.talk.doing !== ""
            text: stage.talk.error !== "" ? "󰀦  " + stage.talk.error : "󰚩  " + stage.talk.doing + " …"
            color: stage.talk.error !== "" ? Theme.red : Theme.muted
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 13 }
        }

        // Frigg; on to text; ending the conversation
        Item {
            width: parent.width
            height: 44
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: "Frigg"
                color: stage.tint
                font { family: Theme.font; pixelSize: 16; bold: true; letterSpacing: 2 }
            }
            Row {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                spacing: 34
                Text {
                    visible: stage.offerText
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Aa"
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 22; bold: true }
                    MouseArea {
                        anchors { fill: parent; margins: -16 }
                        onClicked: stage.textAsked()
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "󰅖"
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 30 }
                    MouseArea {
                        anchors { fill: parent; margins: -16 }
                        onClicked: stage.closeAsked()
                    }
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ({ starting: qsTr("Waking up …"), listening: qsTr("Listening"), thinking: qsTr("Thinking …"),
                     speaking: qsTr("Speaking  ·  talk to interrupt") })[stage.talk.state]
                  ?? (stage.talk.talking ? "" : qsTr("Tap her to talk"))
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
    }
}
