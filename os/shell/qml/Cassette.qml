import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import Mobarchy.Shell

// A compact cassette (Spotify's player): the label with what plays (the album's cover, the
// title, the artists), the reels turning while it plays, and the tape going over from the
// left reel to the right one as the track goes on.
Item {
    id: tape

    property real progress: 0          // 0..1 through the track
    property bool playing: false
    property string title
    property string artists
    property string image
    property color stripe: Theme.accent
    property string side: "A"

    implicitWidth: 340
    implicitHeight: width * 0.638      // (a real one is 100 x 63.8 mm)
    readonly property real w: width
    readonly property real h: height

    // the reels' turn: a little faster on the emptier one, as on a real deck
    property real spin: 0
    NumberAnimation on spin {
        running: tape.playing
        from: tape.spin; to: tape.spin + 360
        duration: 2400
        loops: Animation.Infinite
    }

    // --- the shell -----------------------------------------------------------------------
    Rectangle {
        anchors.fill: parent
        radius: tape.w * 0.035
        gradient: Gradient {
            GradientStop { position: 0; color: "#34343f" }
            GradientStop { position: 1; color: "#1d1d25" }
        }
        border.color: "#4a4a58"
        border.width: Math.max(1, tape.w * 0.004)
    }
    // the screws
    Repeater {
        model: [[0.04, 0.06], [0.96, 0.06], [0.04, 0.94], [0.96, 0.94], [0.5, 0.9]]
        delegate: Rectangle {
            required property var modelData
            width: tape.w * 0.03; height: width; radius: width / 2
            x: tape.w * modelData[0] - width / 2
            y: tape.h * modelData[1] - height / 2
            color: "#5b5b68"
            Rectangle { anchors.centerIn: parent; width: parent.width * 0.7; height: 1.5; color: "#2a2a33"; rotation: 35 }
        }
    }

    // --- the label -----------------------------------------------------------------------
    Rectangle {
        id: label
        x: tape.w * 0.07; y: tape.h * 0.09
        width: tape.w * 0.86; height: tape.h * 0.6
        radius: tape.w * 0.015
        color: "#efe6d2"
        clip: true
        // the album's cover over the whole label
        Image {
            id: cover
            anchors.fill: parent
            visible: tape.image !== "" && status === Image.Ready
            source: tape.image
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            sourceSize.width: 640
        }
        // (no cover: the label's coloured band)
        Rectangle {
            visible: !cover.visible
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; bottomMargin: parent.height * 0.08 }
            height: parent.height * 0.1
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.lighter(tape.stripe, 1.1) }
                GradientStop { position: 1; color: Qt.darker(tape.stripe, 1.3) }
            }
        }
        // a piece of tape stuck on, written on: the title and the artists (over the cover,
        // see-through, its ends torn)
        Item {
            id: tapeStrip
            x: parent.width * 0.04; y: parent.height * 0.05
            width: parent.width * 0.8; height: parent.height * 0.3
            rotation: -1.5
            Shape {
                anchors.fill: parent
                visible: cover.visible
                readonly property real t: height / 6          // a tooth of the torn ends
                ShapePath {
                    fillColor: Qt.rgba(0.97, 0.94, 0.86, 0.72)
                    strokeColor: "transparent"
                    startX: 0; startY: 0
                    PathLine { x: tapeStrip.width; y: 0 }
                    PathLine { x: tapeStrip.width - tapeStrip.height / 12; y: tapeStrip.height / 6 }
                    PathLine { x: tapeStrip.width; y: tapeStrip.height * 2 / 6 }
                    PathLine { x: tapeStrip.width - tapeStrip.height / 12; y: tapeStrip.height * 3 / 6 }
                    PathLine { x: tapeStrip.width; y: tapeStrip.height * 4 / 6 }
                    PathLine { x: tapeStrip.width - tapeStrip.height / 12; y: tapeStrip.height * 5 / 6 }
                    PathLine { x: tapeStrip.width; y: tapeStrip.height }
                    PathLine { x: 0; y: tapeStrip.height }
                    PathLine { x: tapeStrip.height / 12; y: tapeStrip.height * 5 / 6 }
                    PathLine { x: 0; y: tapeStrip.height * 4 / 6 }
                    PathLine { x: tapeStrip.height / 12; y: tapeStrip.height * 3 / 6 }
                    PathLine { x: 0; y: tapeStrip.height * 2 / 6 }
                    PathLine { x: tapeStrip.height / 12; y: tapeStrip.height / 6 }
                    PathLine { x: 0; y: 0 }
                }
            }
            Text {
                id: titleText
                anchors { left: parent.left; right: parent.right; top: parent.top
                          leftMargin: parent.height * 0.25; rightMargin: parent.height * 0.25; topMargin: parent.height * 0.1 }
                text: tape.title || qsTr("Blank tape")
                elide: Text.ElideRight
                color: "#22232d"
                font { family: Theme.font; pixelSize: Math.max(10, tape.h * 0.075); bold: true; italic: true }
            }
            Text {
                anchors { left: titleText.left; right: titleText.right; top: titleText.bottom }
                text: tape.artists
                elide: Text.ElideRight
                color: "#3c3d4d"
                font { family: Theme.font; pixelSize: Math.max(9, tape.h * 0.055); italic: true }
            }
        }
        Text {
            id: sideText
            anchors { right: parent.right; top: parent.top; margins: parent.height * 0.03 }
            text: tape.side
            color: Qt.darker(tape.stripe, 1.2)
            font { family: Theme.font; pixelSize: tape.h * 0.11; bold: true }
        }
    }

    // --- the window, with the reels ------------------------------------------------------
    Rectangle {
        id: windowMask
        x: window.x; y: window.y; width: window.width; height: window.height
        radius: window.radius
        layer.enabled: true
        visible: false
    }
    Rectangle {
        id: window
        x: tape.w * 0.25; y: tape.h * 0.3
        width: tape.w * 0.5; height: tape.h * 0.28
        radius: height * 0.5
        color: "#15151b"
        border.color: "#0b0b0f"
        clip: true
        // cut to its round ends (clip alone cuts square), so that the label shows around them
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: windowMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        // the tape wound on each reel: from the left one to the right one
        readonly property real rMin: tape.w * 0.06
        readonly property real rMax: tape.w * 0.15
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                readonly property real fill: index === 0 ? 1 - tape.progress : tape.progress
                property real r: window.rMin + (window.rMax - window.rMin) * Math.sqrt(Math.max(0, Math.min(1, fill)))
                width: 2 * r; height: width; radius: r
                x: (index === 0 ? window.width * 0.2 : window.width * 0.8) - r
                y: window.height / 2 - r
                color: "#4a2f1f"
                border.color: "#6b4630"
                Behavior on r { NumberAnimation { duration: 600 } }
            }
        }
        // the hubs, turning
        Repeater {
            model: 2
            delegate: Item {
                required property int index
                width: tape.w * 0.085; height: width
                x: (index === 0 ? window.width * 0.2 : window.width * 0.8) - width / 2
                y: window.height / 2 - height / 2
                rotation: tape.spin * (index === 0 ? 1 + tape.progress : 2 - tape.progress)
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "#e9e4d8"
                }
                // the teeth (each in an item turned about the hub's middle; the shell's own
                // Rotation type hides QtQuick's)
                Repeater {
                    model: 6
                    delegate: Item {
                        required property int index
                        anchors.fill: parent
                        rotation: index * 60
                        Rectangle {
                            x: parent.width / 2 - width / 2
                            y: parent.height / 2 - height
                            width: parent.width * 0.12; height: parent.height * 0.42
                            color: "#15151b"
                        }
                    }
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: parent.width * 0.32; height: width; radius: width / 2
                    color: "#15151b"
                }
            }
        }
        // the glass
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "transparent"
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 0.45; color: "transparent" }
            }
        }
    }

    // --- the bottom: the head's opening ---------------------------------------------------
    Shape {
        anchors.fill: parent
        ShapePath {
            fillColor: "#25252e"
            strokeColor: "#4a4a58"
            strokeWidth: 1
            startX: tape.w * 0.19; startY: tape.h
            PathLine { x: tape.w * 0.24; y: tape.h * 0.77 }
            PathLine { x: tape.w * 0.76; y: tape.h * 0.77 }
            PathLine { x: tape.w * 0.81; y: tape.h }
        }
    }
    // the tape across the head, and the holes
    Rectangle {
        x: tape.w * 0.27; y: tape.h * 0.955
        width: tape.w * 0.46; height: Math.max(2, tape.h * 0.02)
        color: "#4a2f1f"
    }
    Repeater {
        model: [0.31, 0.43, 0.57, 0.69]
        delegate: Rectangle {
            required property var modelData
            width: tape.w * 0.035; height: width; radius: width / 2
            x: tape.w * modelData - width / 2
            y: tape.h * 0.86 - height / 2
            color: "#0f0f14"
        }
    }
}
