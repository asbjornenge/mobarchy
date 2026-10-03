pragma Singleton
import QtQuick

// Tokyo Night, Omarchy's default theme.
QtObject {
    readonly property color bg: "#1a1b26"
    readonly property color bgDark: "#16161e"
    readonly property color panel: "#24283b"
    readonly property color line: "#292e42"
    readonly property color fg: "#c0caf5"
    readonly property color muted: "#565f89"
    readonly property color accent: "#7aa2f7"
    readonly property color green: "#9ece6a"
    readonly property color red: "#f7768e"
    readonly property color magenta: "#bb9af7"
    readonly property string font: "JetBrainsMono Nerd Font"

    // Scrolling like a phone: a flick throws the list (Qt's defaults are a desktop's, a
    // slow wheel-like glide), and the ends bounce (Flickable.DragAndOvershootBounds).
    readonly property real flickVelocity: 9000      // logical px/s
    readonly property real flickDeceleration: 1400  // logical px/s²
    // How long an end takes to settle back after a bounce: Qt's default snaps back.
    readonly property int reboundDuration: 550
}
