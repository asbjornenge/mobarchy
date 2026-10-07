import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Frigg (ADR 0026): the live conversation, over whatever is on the screen. An orb for what
// Frigg is doing (listening, thinking, speaking), what was heard and what Frigg says.
// A tap on the orb interrupts; ✕ ends the conversation.
LayerWindow {
    id: overlay

    required property Talk talk
    property int insetBottom: 0

    shown: talk.active
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

    Column {
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
            text: overlay.talk.heard ? "«" + overlay.talk.heard + "»" : ""
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

        // the orb
        Item {
            width: parent.width
            height: 150
            Rectangle {
                id: halo
                anchors.centerIn: orb
                width: orb.width; height: width; radius: width / 2
                color: "transparent"
                border { color: overlay.tint; width: 3 }
                opacity: 0
                SequentialAnimation on scale {
                    running: overlay.talk.state === "listening" || overlay.talk.state === "speaking"
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 1.5; duration: overlay.talk.state === "speaking" ? 600 : 1200; easing.type: Easing.OutCubic }
                }
                SequentialAnimation on opacity {
                    running: overlay.talk.state === "listening" || overlay.talk.state === "speaking"
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.8; to: 0; duration: overlay.talk.state === "speaking" ? 600 : 1200 }
                }
            }
            Rectangle {
                id: orb
                anchors.centerIn: parent
                width: 104; height: 104; radius: 52
                color: overlay.tint
                Behavior on color { ColorAnimation { duration: 250 } }
                // thinking: a turning arc
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -10
                    radius: width / 2
                    color: "transparent"
                    visible: overlay.talk.state === "thinking" || overlay.talk.state === "starting"
                    border { color: Theme.fg; width: 3 }
                    opacity: 0.5
                    RotationAnimation on rotation {
                        running: overlay.talk.state === "thinking" || overlay.talk.state === "starting"
                        loops: Animation.Infinite
                        from: 0; to: 360; duration: 1400
                    }
                    Rectangle { width: 14; height: 14; radius: 7; color: Theme.fg; x: parent.width / 2 - 7; y: -7 }
                }
                Text {
                    anchors.centerIn: parent
                    text: overlay.talk.state === "speaking" ? "󰕾" : overlay.talk.state === "thinking" ? "󰚩" : "󰍬"
                    color: Theme.bg
                    font { family: Theme.font; pixelSize: 40 }
                }
                MouseArea { anchors.fill: parent; onClicked: overlay.talk.interrupt() }
            }
            Text {
                anchors { right: parent.right; verticalCenter: orb.verticalCenter }
                text: "󰅖"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 30 }
                MouseArea {
                    anchors { fill: parent; margins: -18 }
                    onClicked: overlay.talk.stop()
                }
            }
            Text {
                anchors { left: parent.left; verticalCenter: orb.verticalCenter }
                text: "Frigg"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 16; bold: true }
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
