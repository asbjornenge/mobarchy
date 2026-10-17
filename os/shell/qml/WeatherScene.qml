pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// A small living picture of the weather (WMO code): the sun's rays turning, the moon and
// twinkling stars at night, clouds drifting, rain falling, snow drifting down, fog sliding,
// lightning now and then. Drawn from plain shapes in the theme's colours (the sun warm);
// it moves only while `running` (the home board in sight), else it stands still.
Item {
    id: scene

    property int code: 0
    property bool day: true
    property bool running: true
    // the card behind it (the moon's shadow is cut out of it)
    property color ground: Theme.panel

    implicitWidth: 52
    implicitHeight: 52

    readonly property string kind: code === 0 ? "clear"
                                 : code <= 2 ? "partly"
                                 : code === 3 ? "cloudy"
                                 : code <= 48 ? "fog"
                                 : code <= 67 || (code >= 80 && code <= 82) ? "rain"
                                 : code <= 86 ? "snow"
                                 : "thunder"
    readonly property real s: Math.min(width, height)
    readonly property color sunColor: "#f6c453"
    // (solid: a cloud is round shapes over each other, and see-through ones would show it)
    readonly property color cloudColor: Qt.tint(ground, Qt.alpha(Theme.fg, 0.88))
    readonly property color cloudBack: Qt.tint(ground, Qt.alpha(Theme.muted, 0.9))

    // --- the sun ---------------------------------------------------------------------------
    component Sun: Item {
        id: sun
        property real size: scene.s * 0.62
        width: size; height: size
        Item {
            anchors.fill: parent
            RotationAnimation on rotation {
                running: scene.running
                from: 0; to: 360; duration: 24000; loops: Animation.Infinite
            }
            Repeater {
                model: 8
                // (each ray at the top of a square turned about the sun's middle; Rotation is
                // the shell's own type here)
                delegate: Item {
                    required property int index
                    width: sun.size; height: sun.size
                    rotation: index * 45
                    Rectangle {
                        width: sun.size * 0.09
                        height: sun.size * 0.2
                        radius: width / 2
                        color: scene.sunColor
                        x: sun.size / 2 - width / 2
                    }
                }
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: sun.size * 0.52; height: width; radius: width / 2
            color: scene.sunColor
            SequentialAnimation on scale {
                running: scene.running
                loops: Animation.Infinite
                NumberAnimation { from: 0.94; to: 1.06; duration: 2200; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1.06; to: 0.94; duration: 2200; easing.type: Easing.InOutSine }
            }
        }
    }

    // --- the moon and stars ------------------------------------------------------------------
    component Moon: Item {
        id: moon
        property real size: scene.s * 0.5
        width: size; height: size
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Qt.alpha(Theme.fg, 0.95)
        }
        Rectangle {   // the shadow that makes it a crescent
            x: moon.size * 0.32; y: -moon.size * 0.12
            width: moon.size; height: width; radius: width / 2
            color: scene.ground
        }
    }
    component Star: Rectangle {
        id: star
        property int delay: 0
        width: Math.max(2, scene.s * 0.06); height: width; radius: width / 2
        color: Theme.fg
        SequentialAnimation on opacity {
            running: scene.running
            loops: Animation.Infinite
            PauseAnimation { duration: star.delay }
            NumberAnimation { from: 1; to: 0.15; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.15; to: 1; duration: 900; easing.type: Easing.InOutSine }
            PauseAnimation { duration: 1400 - star.delay }
        }
    }

    // --- a cloud: three round tops on a flat bottom, drifting a little --------------------------
    component Cloud: Item {
        id: cloud
        property real size: scene.s * 0.72
        property color tint: scene.cloudColor
        property real drift: scene.s * 0.06
        property int period: 5200
        width: size; height: size * 0.6
        Rectangle { x: 0; y: cloud.height * 0.42; width: cloud.size; height: cloud.height * 0.58
                    radius: height / 2; color: cloud.tint }
        Rectangle { x: cloud.size * 0.14; y: cloud.height * 0.3; width: cloud.size * 0.38; height: width
                    radius: width / 2; color: cloud.tint }
        Rectangle { x: cloud.size * 0.36; y: 0; width: cloud.size * 0.48; height: width
                    radius: width / 2; color: cloud.tint }
        transform: Translate {
            SequentialAnimation on x {
                running: scene.running
                loops: Animation.Infinite
                NumberAnimation { from: -cloud.drift; to: cloud.drift; duration: cloud.period; easing.type: Easing.InOutSine }
                NumberAnimation { from: cloud.drift; to: -cloud.drift; duration: cloud.period; easing.type: Easing.InOutSine }
            }
        }
    }

    // --- falling things: rain and snow -------------------------------------------------------
    component Fall: Item {
        id: fall
        property bool snow: false
        property int count: 4
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                  topMargin: scene.s * 0.6 }
        clip: true
        Repeater {
            model: fall.count
            delegate: Rectangle {
                id: drop
                required property int index
                readonly property int period: fall.snow ? 2600 : 900
                x: scene.s * (0.2 + 0.18 * index)
                width: fall.snow ? scene.s * 0.09 : scene.s * 0.045
                height: fall.snow ? width : scene.s * 0.16
                radius: width / 2
                color: fall.snow ? Theme.fg : Theme.accent
                opacity: 0
                SequentialAnimation {
                    running: scene.running
                    loops: Animation.Infinite
                    PauseAnimation { duration: drop.index * drop.period / fall.count }
                    ParallelAnimation {
                        NumberAnimation { target: drop; property: "y"; from: -drop.height; to: fall.height
                                          duration: drop.period; easing.type: fall.snow ? Easing.Linear : Easing.InQuad }
                        SequentialAnimation {
                            NumberAnimation { target: drop; property: "opacity"; from: 0; to: 1; duration: drop.period * 0.2 }
                            PauseAnimation { duration: drop.period * 0.5 }
                            NumberAnimation { target: drop; property: "opacity"; from: 1; to: 0; duration: drop.period * 0.3 }
                        }
                    }
                    PauseAnimation { duration: (fall.count - 1 - drop.index) * drop.period / fall.count }
                }
                // snow sways as it falls
                transform: Translate {
                    SequentialAnimation on x {
                        running: scene.running && fall.snow
                        loops: Animation.Infinite
                        NumberAnimation { from: -scene.s * 0.05; to: scene.s * 0.05; duration: 1300; easing.type: Easing.InOutSine }
                        NumberAnimation { from: scene.s * 0.05; to: -scene.s * 0.05; duration: 1300; easing.type: Easing.InOutSine }
                    }
                }
            }
        }
    }

    // --- the scenes ---------------------------------------------------------------------------
    // clear
    Sun { visible: scene.kind === "clear" && scene.day; anchors.centerIn: parent; size: scene.s * 0.86 }
    Item {
        visible: scene.kind === "clear" && !scene.day
        anchors.fill: parent
        Moon { anchors.centerIn: parent; size: scene.s * 0.56 }
        Star { x: scene.s * 0.08; y: scene.s * 0.14; delay: 0 }
        Star { x: scene.s * 0.84; y: scene.s * 0.08; delay: 500 }
        Star { x: scene.s * 0.78; y: scene.s * 0.82; delay: 1000 }
    }
    // partly cloudy: the sun (or moon) behind a cloud
    Item {
        visible: scene.kind === "partly"
        anchors.fill: parent
        Sun { visible: scene.day; x: scene.s * 0.3; y: scene.s * 0.02; size: scene.s * 0.66 }
        Moon { visible: !scene.day; x: scene.s * 0.46; y: scene.s * 0.1; size: scene.s * 0.4 }
        Cloud { x: 0; y: scene.s * 0.42; size: scene.s * 0.74 }
    }
    // cloudy, and the clouds over rain, snow and thunder
    Item {
        visible: scene.kind === "cloudy" || scene.kind === "rain" || scene.kind === "snow" || scene.kind === "thunder"
        anchors.fill: parent
        Cloud { x: scene.s * 0.3; y: scene.s * 0.06; size: scene.s * 0.6; tint: scene.cloudBack; period: 6800 }
        Cloud { x: scene.s * 0.04; y: scene.s * (scene.kind === "cloudy" ? 0.34 : 0.18); size: scene.s * 0.76 }
    }
    Fall { visible: scene.kind === "rain" || scene.kind === "thunder"; count: 4 }
    Fall { visible: scene.kind === "snow"; snow: true; count: 4 }
    // a flash of lightning now and then
    Text {
        visible: scene.kind === "thunder"
        x: scene.s * 0.38; y: scene.s * 0.46
        text: "󱐋"
        color: scene.sunColor
        font { family: Theme.font; pixelSize: scene.s * 0.42 }
        opacity: 0
        SequentialAnimation on opacity {
            running: scene.running && scene.kind === "thunder"
            loops: Animation.Infinite
            PauseAnimation { duration: 2600 }
            NumberAnimation { to: 1; duration: 60 }
            NumberAnimation { to: 0.2; duration: 80 }
            NumberAnimation { to: 1; duration: 60 }
            NumberAnimation { to: 0; duration: 300 }
        }
    }
    // fog: bands sliding past each other
    Repeater {
        model: scene.kind === "fog" ? 3 : 0
        delegate: Rectangle {
            id: band
            required property int index
            x: scene.s * 0.1
            y: scene.s * (0.3 + 0.2 * index)
            width: scene.s * (0.8 - 0.12 * (index % 2))
            height: scene.s * 0.08
            radius: height / 2
            color: Qt.alpha(Theme.fg, 0.75 - 0.15 * index)
            transform: Translate {
                SequentialAnimation on x {
                    running: scene.running
                    loops: Animation.Infinite
                    NumberAnimation { from: -scene.s * 0.07 * (band.index % 2 ? -1 : 1); to: scene.s * 0.07 * (band.index % 2 ? -1 : 1)
                                      duration: 3200; easing.type: Easing.InOutSine }
                    NumberAnimation { from: scene.s * 0.07 * (band.index % 2 ? -1 : 1); to: -scene.s * 0.07 * (band.index % 2 ? -1 : 1)
                                      duration: 3200; easing.type: Easing.InOutSine }
                }
            }
        }
    }
}
