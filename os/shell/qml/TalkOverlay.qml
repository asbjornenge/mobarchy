pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Frigg (ADR 0026): the live conversation, over whatever is on the screen. An orb for what
// Frigg is doing (listening, thinking, speaking), what was heard and what Frigg says.
// A tap on the orb interrupts; ✕ ends the conversation. At the top (on an unlocked phone),
// whether "Hei Frigg" starts one: always, while charging, or not; and learning the owner's
// voice, which then shows the sentences to read.
LayerWindow {
    id: overlay

    required property Talk talk
    required property FriggSettings settings
    property bool locked: false
    property int insetBottom: 0

    // (not while Frigg uses the screen: she would see and tap her own overlay)
    shown: talk.talking && !talk.usingScreen
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-talk"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    readonly property color tint: talk.state === "speaking" ? Theme.green
                                : talk.state === "thinking" ? Theme.magenta : Theme.accent

    Rectangle {
        anchors.fill: parent
        color: "#c0000000"
        MouseArea { anchors.fill: parent }
    }

    // "Hei Frigg": always · while charging · off
    Row {
        id: wakeRow
        visible: !overlay.locked && !overlay.talk.enrolling
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 64 }
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
                readonly property bool chosen: overlay.settings.wake === modelData.id
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
                    onClicked: overlay.settings.wake = choice.modelData.id
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
            onClicked: overlay.talk.enroll()
        }
    }

    // While learning the voice: the sentence to read
    Column {
        visible: overlay.talk.enrolling && overlay.talk.enrollText !== ""
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 28; topMargin: 120 }
        spacing: 16
        Text {
            text: qsTr("Read aloud  ·  %1 of %2").arg(overlay.talk.enrollIndex + 1).arg(overlay.talk.enrollCount)
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
        Text {
            width: parent.width
            text: overlay.talk.enrollText
            color: Theme.fg
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 26 }
        }
    }

    // Frigg's orb, large, in the room the words leave: it grows in when the conversation
    // starts (from the home board's Frigg card, or "Hei Frigg"). A tap interrupts her.
    Item {
        id: stage
        visible: !overlay.talk.enrolling
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: talkColumn.top
                  topMargin: wakeRow.visible ? 170 : 60; bottomMargin: 8 }
        readonly property real size: Math.min(width - 40, height)
        Shape {     // the glow
            anchors.centerIn: big
            width: big.width * 1.5; height: width
            opacity: 0.55
            ShapePath {
                strokeWidth: -1
                fillGradient: RadialGradient {
                    centerX: big.width * 0.75; centerY: big.width * 0.75; centerRadius: big.width * 0.75
                    focalX: centerX; focalY: centerY
                    GradientStop { position: 0; color: Qt.alpha(overlay.tint, 0.5) }
                    GradientStop { position: 0.45; color: Qt.alpha(overlay.tint, 0.15) }
                    GradientStop { position: 1; color: "transparent" }
                }
                PathRectangle { width: big.width * 1.5; height: big.width * 1.5 }
            }
        }
        FriggOrb {
            id: big
            anchors.centerIn: parent
            width: stage.size; height: width
            points: 440
            color: overlay.tint
            state: overlay.talk.state
            running: overlay.visible && stage.visible
            Behavior on color { ColorAnimation { duration: 400 } }
            // growing in
            scale: overlay.shown ? 1 : 0.35
            opacity: overlay.shown ? 1 : 0
            Behavior on scale { NumberAnimation { duration: 520; easing.type: Easing.OutBack } }
            Behavior on opacity { NumberAnimation { duration: 300 } }
            MouseArea { anchors.fill: parent; onClicked: overlay.talk.interrupt() }
        }
    }

    Column {
        id: talkColumn
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  margins: 28; bottomMargin: 48 + overlay.insetBottom }
        spacing: 22

        // what Frigg says, and what was heard
        Text {
            width: parent.width
            visible: text !== ""
            text: overlay.talk.said
            color: Theme.fg
            wrapMode: Text.Wrap
            maximumLineCount: 8
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 20 }
        }
        Text {
            width: parent.width
            visible: text !== ""
            text: overlay.talk.heard ? "«" + overlay.talk.heard + "»"
                  + (overlay.talk.guest ? "  ·  " + qsTr("not the owner's voice") : "") : ""
            color: Theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 15; italic: true }
        }
        Text {
            width: parent.width
            visible: overlay.talk.error !== "" || overlay.talk.doing !== ""
            text: overlay.talk.error !== "" ? "󰀦  " + overlay.talk.error : "󰚩  " + overlay.talk.doing + " …"
            color: overlay.talk.error !== "" ? Theme.red : Theme.muted
            wrapMode: Text.Wrap
            font { family: Theme.font; pixelSize: 13 }
        }

        // Frigg, and ending the conversation
        Item {
            width: parent.width
            height: 44
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: "Frigg"
                color: overlay.tint
                font { family: Theme.font; pixelSize: 16; bold: true; letterSpacing: 2 }
            }
            Text {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                text: "󰅖"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 30 }
                MouseArea {
                    anchors { fill: parent; margins: -18 }
                    onClicked: overlay.talk.stop()
                }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: ({ starting: qsTr("Waking up …"), listening: qsTr("Listening"), thinking: qsTr("Thinking …"),
                     speaking: qsTr("Speaking  ·  talk to interrupt"), idle: "" })[overlay.talk.state] || ""
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
    }
}
