pragma ComponentBehavior: Bound

import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The emoji, from a hardware keyboard: Alt+Space (Hyprland's bind, Keyboard.emojiSearch) puts
// this up over the bottom of the screen and borrows the keyboard's keys (Keyboard.grabKeys)
// from the app with the focused field. Typing searches (the on-screen keyboard's emoji
// search: CLDR's names, Norwegian and English), the arrows and Tab choose, Return types the
// one chosen into the field, Escape goes. With nothing typed: the recently used.
LayerWindow {
    id: picker

    required property Keyboard keyboard
    // (the on-screen keyboard's: its recent emoji, and typing one the way it does)
    required property KeyboardPanel panel

    shown: false
    width: Math.min(Screen.width - 32, 560)
    height: 196
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-emoji"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom
    LayerShell.Window.margins.bottom: 12
    // (above the bottom edge: it respects the edge's exclusive zone)
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    property string query: ""
    property int chosen: 0
    // no field focused: nothing to type into, said for a moment
    property bool noField: false
    readonly property var found: query.trim() !== "" ? keyboard.searchEmoji(query, 48)
                                                     : panel.recentEmoji.slice(0, 48)
    readonly property int columns: Math.max(1, Math.floor((width - 24) / 48))
    onFoundChanged: chosen = 0

    function open() {
        query = ""
        chosen = 0
        noField = !keyboard.grabKeys()
        shown = true
        if (noField)
            noFieldTimer.restart()
    }
    function close() {
        keyboard.releaseKeys()
        shown = false
    }
    function pick(e) {
        if (e)
            panel.typeEmoji(e)
        close()
    }
    function move(by) {
        if (found.length)
            chosen = Math.max(0, Math.min(found.length - 1, chosen + by))
    }
    Timer { id: noFieldTimer; interval: 1600; onTriggered: picker.shown = false }

    Connections {
        target: picker.keyboard
        function onEmojiSearchAsked() { picker.shown && !picker.noField ? picker.close() : picker.open() }
        // (the field lost focus: the keys went back with it)
        function onGrabbingChanged() { if (!picker.keyboard.grabbing && picker.shown && !picker.noField) picker.shown = false }
        function onGrabbedText(text) { picker.query += text }
        function onGrabbedKey(name) {
            switch (name) {
            case "BackSpace": picker.query = picker.query.slice(0, -1); break
            case "Return": picker.pick(picker.found[picker.chosen]); break
            case "Escape": picker.close(); break
            case "Left": picker.move(-1); break
            case "Right": case "Tab": picker.move(1); break
            case "Up": picker.move(-picker.columns); break
            case "Down": picker.move(picker.columns); break
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: Theme.panel
        border.color: Theme.accent

        // what is typed
        Text {
            id: line
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
            elide: Text.ElideLeft
            font { family: Theme.font; pixelSize: 15 }
            color: picker.noField || picker.query === "" ? Theme.muted : Theme.fg
            text: picker.noField ? qsTr("Tap a text field first")
                : picker.query === "" ? "󰍉  " + qsTr("Search emoji  ·  recent") : picker.query + "▏"
        }

        // what it finds (or the recent ones): a tap, or Return on the chosen one, types it
        GridView {
            id: grid
            visible: !picker.noField
            anchors { left: parent.left; right: parent.right; top: line.bottom; bottom: parent.bottom
                      leftMargin: 12; rightMargin: 12; topMargin: 8; bottomMargin: 10 }
            clip: true
            cellWidth: width / picker.columns
            cellHeight: 48
            model: picker.found
            currentIndex: picker.chosen
            boundsBehavior: Flickable.StopAtBounds
            highlightFollowsCurrentItem: true
            highlight: Rectangle { radius: 12; color: Theme.line; border.color: Theme.accent }
            delegate: Item {
                id: cell
                required property string modelData
                required property int index
                width: grid.cellWidth; height: grid.cellHeight
                Text {
                    anchors.centerIn: parent
                    text: cell.modelData
                    font { family: "Noto Color Emoji"; pixelSize: 28 }
                }
                TapHandler { onTapped: picker.pick(cell.modelData) }
            }
            Text {
                anchors.centerIn: parent
                visible: grid.count === 0
                text: picker.query.trim() !== "" ? qsTr("Nothing found") : qsTr("Type to search")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }
}
