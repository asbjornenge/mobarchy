import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// "Copy": when text is selected (marked) in an app, a button over the bottom of the screen
// copies it to the clipboard (ADR 0016). Selecting puts the text in the primary selection,
// which the shell sees, so this works in every app without a menu of its own. It goes away
// after a few seconds, or when tapped.
LayerWindow {
    id: bar

    required property Clipboard clipboard

    shown: false
    width: Math.min(420, pill.implicitWidth)
    height: 52
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-copy"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom
    LayerShell.Window.exclusionZone: -1     // over everything, the keyboard included
    LayerShell.Window.margins.bottom: 44       // clear of the bottom edge's handle
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    property bool copied: false
    property string text: ""        // what is offered (the selection when it appeared)

    // Offer text for copying (the shell's own selections, e.g. in the conversation).
    function offer(text) {
        if (text.trim() === "")
            return
        bar.text = text
        bar.copied = false
        bar.shown = true
        hide.restart()
    }

    Connections {
        target: bar.clipboard
        function onSelectedChanged() {
            const selected = bar.clipboard.selected
            if (selected.trim() === "") {
                // deselected: nothing to copy any more
                if (!bar.copied)
                    bar.shown = false
                return
            }
            bar.offer(selected)
        }
    }
    Timer {
        id: hide
        interval: bar.copied ? 1200 : 6000
        onTriggered: bar.shown = false
    }

    Rectangle {
        id: pill
        anchors.centerIn: parent
        implicitWidth: label.implicitWidth + 36
        width: Math.min(bar.width, implicitWidth)
        height: 44
        radius: height / 2
        color: tap.pressed ? Theme.line : Theme.panel
        border.color: bar.copied ? Theme.green : Theme.accent
        Text {
            id: label
            anchors { fill: parent; leftMargin: 18; rightMargin: 18 }
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: bar.copied ? Theme.green : Theme.fg
            font { family: Theme.font; pixelSize: 15 }
            text: bar.copied ? "✓ " + qsTr("Kopiert")
                  : "󰆏  " + qsTr("Kopier") + "  «" + bar.text.replace(/\s+/g, " ").slice(0, 40) + "»"
        }
        TapHandler {
            id: tap
            onTapped: {
                bar.clipboard.setText(bar.text)
                bar.copied = true
                hide.restart()
            }
        }
    }
}
