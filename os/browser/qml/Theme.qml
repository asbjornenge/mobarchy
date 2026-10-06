pragma Singleton
import QtQuick

// The shell's theme, live (ADR 0025): the same theme files, through the same ThemeSource.
QtObject {
    readonly property ThemeSource source: ThemeSource {}
    readonly property color bg: source.colors.bg
    readonly property color bgDark: source.colors.bgDark
    readonly property color panel: source.colors.panel
    readonly property color line: source.colors.line
    readonly property color fg: source.colors.fg
    readonly property color muted: source.colors.muted
    readonly property color accent: source.colors.accent
    readonly property color green: source.colors.green
    readonly property color red: source.colors.red
    readonly property color magenta: source.colors.magenta
    readonly property string font: source.font

    // Scrolling like a phone: a flick throws the list (Qt's defaults are a desktop's, a
    // slow wheel-like glide), and the ends bounce (Flickable.DragAndOvershootBounds).
    readonly property real flickVelocity: 9000      // logical px/s
    readonly property real flickDeceleration: 1400  // logical px/s²
    // How long an end takes to settle back after a bounce: Qt's default snaps back.
    readonly property int reboundDuration: 550
}
