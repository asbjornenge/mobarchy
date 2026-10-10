pragma Singleton
import QtQuick

// The active theme (ADR 0025: theme files, followed live; the agent makes new ones).
// Mobarchy's own is Tokyo Night, Omarchy's default.
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
    // the wallpaper (lock screen, home), "" without one
    readonly property string wallpaper: source.wallpaper

    // Scrolling like a phone: a flick throws the list (Qt's defaults are a desktop's, a
    // slow wheel-like glide), and the ends are firm: a finger can pull past one (Qt halves
    // the pull), a flick stops at it (Qt's overshoot, up to 150 px, is loose on a phone).
    readonly property real flickVelocity: 9000      // logical px/s
    readonly property real flickDeceleration: 1400  // logical px/s²
    readonly property int boundsBehavior: Flickable.DragOverBounds
    // How long an end takes to settle back after it was pulled past.
    readonly property int reboundDuration: 280
}
