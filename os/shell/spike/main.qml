// Mobarchy shell spike (measured for ADR 0009): agent-first home, Omarchy look, swipe navigation.
// Measures what Qt Quick costs on the phone: startup, memory, frame rate.
//
//   QML_XHR_ALLOW_FILE_READ=1 qml main.qml
//
// Not the shell yet: data is read straight from sysfs, and the feed is static.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import org.kde.layershell as LayerShell

QtObject {
    id: shell

    // Tokyo Night (Omarchy's default theme)
    readonly property QtObject theme: QtObject {
        readonly property color bg: "#1a1b26"
        readonly property color bgDark: "#16161e"
        readonly property color panel: "#24283b"
        readonly property color fg: "#c0caf5"
        readonly property color muted: "#565f89"
        readonly property color accent: "#7aa2f7"
        readonly property color green: "#9ece6a"
        readonly property color magenta: "#bb9af7"
        readonly property string font: "JetBrainsMono Nerd Font"
    }

    property string battery: "?"
    property string batteryState: ""
    property string clock: Qt.formatTime(new Date(), "HH:mm")

    function readFile(path) {
        const xhr = new XMLHttpRequest()
        xhr.open("GET", "file://" + path, false)
        xhr.send()
        return xhr.responseText.trim()
    }

    property Timer ticker: Timer {
        interval: 5000; running: true; repeat: true; triggeredOnStart: true
        onTriggered: {
            shell.clock = Qt.formatTime(new Date(), "HH:mm")
            shell.battery = shell.readFile("/sys/class/power_supply/battery/capacity")
            shell.batteryState = shell.readFile("/sys/class/power_supply/battery/status")
        }
    }

    // --- status bar: a layer-shell surface along the top edge ---------------------------
    property Window statusBar: Window {
        visible: true
        height: 30
        color: shell.theme.bgDark
        LayerShell.Window.layer: LayerShell.Window.LayerTop
        LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft
                                   | LayerShell.Window.AnchorRight
        LayerShell.Window.exclusionZone: height
        LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

        Text {
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
            text: shell.clock
            color: shell.theme.fg; font { family: shell.theme.font; pixelSize: 14; bold: true }
        }
        Text {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            text: "󰖩  " + (shell.batteryState === "Charging" ? "󰂄 " : "󰁹 ") + shell.battery + "%"
            color: shell.theme.fg; font { family: shell.theme.font; pixelSize: 13 }
        }
    }

    // --- home: the agent feed (a normal window: text input works there) ------------------
    property Window home: Window {
        id: home
        visible: true
        title: "Mobarchy"
        color: shell.theme.bg

        // Frame counter for the measurements. FrameAnimation renders every frame (it keeps
        // the GPU busy): only for measuring, never in the real shell.
        property int frames: 0
        property real fps: 0
        FrameAnimation { running: true; onTriggered: home.frames++ }
        Timer { interval: 1000; running: true; repeat: true
                onTriggered: { home.fps = home.frames; home.frames = 0 } }

        ListView {
            id: feed
            anchors { fill: parent; margins: 16; bottomMargin: 96 }
            spacing: 12
            clip: true
            model: ListModel {
                ListElement { who: "mobarchy"; body: "God kveld. Hva vil du gjøre?" }
                ListElement { who: "status"; body: "Wifi: home · GPU: Mali-G715 · 120 Hz" }
                ListElement { who: "you"; body: "vis meg været i morgen" }
                ListElement { who: "mobarchy"; body: "Opphold og 12° i Oslo i morgen. Vil du ha et varsel kl 07?" }
                ListElement { who: "task"; body: "✓ Koblet til wifi «home»" }
                ListElement { who: "task"; body: "✓ Installerte Firefox 157" }
            }
            delegate: Rectangle {
                width: ListView.view.width
                height: msg.implicitHeight + 24
                radius: 10
                color: who === "you" ? shell.theme.panel : "transparent"
                border.color: who === "task" ? shell.theme.green : (who === "status" ? shell.theme.muted : "transparent")
                border.width: 1
                Text {
                    id: msg
                    anchors { fill: parent; margins: 12 }
                    wrapMode: Text.Wrap
                    text: (who === "you" ? "❯ " : who === "mobarchy" ? "✦ " : "") + body
                    color: who === "mobarchy" ? shell.theme.accent : who === "task" ? shell.theme.green : shell.theme.fg
                    font { family: shell.theme.font; pixelSize: 15 }
                }
            }
        }

        // Ask field
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16; bottomMargin: 40 }
            height: 46; radius: 23
            color: shell.theme.panel
            border.color: ask.activeFocus ? shell.theme.accent : shell.theme.muted
            TextInput {
                id: ask
                anchors { fill: parent; leftMargin: 18; rightMargin: 18 }
                verticalAlignment: TextInput.AlignVCenter
                color: shell.theme.fg; font { family: shell.theme.font; pixelSize: 15 }
                Text { visible: !ask.text && !ask.activeFocus; anchors.verticalCenter: parent.verticalCenter
                       text: "✦ Spør Mobarchy …"; color: shell.theme.muted
                       font { family: shell.theme.font; pixelSize: 15 } }
            }
        }

        Text {
            anchors { right: parent.right; top: parent.top; margins: 8 }
            text: home.fps + " fps"; color: shell.theme.muted
            font { family: shell.theme.font; pixelSize: 11 }
        }

        // --- swipe up from the bottom edge: app drawer ----------------------------------
        Rectangle {
            id: drawer
            width: parent.width; height: parent.height * 0.75
            y: parent.height - drag.openness * height
            radius: 18
            color: shell.theme.panel
            // Follows the finger while dragging; animates when it snaps open or closed.
            Behavior on y { enabled: !dragArea.pressed; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            Text { anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 10 }
                   text: "──"; color: shell.theme.muted; font.pixelSize: 18 }
            GridView {
                anchors { fill: parent; topMargin: 40; margins: 16 }
                cellWidth: width / 4; cellHeight: 96
                model: ["Terminal", "Firefox", "Web", "Files", "Settings", "Camera", "Music", "Notes"]
                delegate: Column {
                    width: GridView.view.cellWidth; spacing: 6
                    Rectangle { width: 56; height: 56; radius: 16; anchors.horizontalCenter: parent.horizontalCenter
                                color: shell.theme.bg; border.color: shell.theme.accent
                                Text { anchors.centerIn: parent; text: modelData[0]; color: shell.theme.accent
                                       font { family: shell.theme.font; pixelSize: 22; bold: true } } }
                    Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: modelData
                           color: shell.theme.fg; font { family: shell.theme.font; pixelSize: 12 } }
                }
            }
        }
        // Drag state: 0 = closed, 1 = open; follows the finger, then snaps.
        QtObject { id: drag; property real openness: 0 }
        MouseArea {
            id: dragArea
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: drag.openness > 0.5 ? parent.height : 36
            property real startY: 0; property real startOpen: 0
            onPressed: (m) => { startY = m.y + y; startOpen = drag.openness }
            onPositionChanged: (m) => {
                const dy = startY - (m.y + y)
                drag.openness = Math.max(0, Math.min(1, startOpen + dy / drawer.height))
            }
            onReleased: drag.openness = drag.openness > 0.35 ? 1 : 0
        }
    }
}
