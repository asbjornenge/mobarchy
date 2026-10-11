import QtQuick
import QtMultimedia
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// An alarm or a timer ringing (ADR 0028): over everything, the lock screen too, with a
// chime that grows louder and a buzz, until it is snoozed (an alarm: 9 minutes) or stopped.
// It stops by itself after 10 minutes. A phone turned all the way down or muted is turned
// up for it (to half), and back after.
LayerWindow {
    id: screen

    required property Alarms alarms
    required property Haptics haptics
    required property Volume volume

    shown: alarms.ringing
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-alarm"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    readonly property bool isTimer: alarms.kind === "timer"

    // --- sound, buzz, the system volume -------------------------------------------------
    property real savedVolume: -1
    property bool savedMuted: false
    function startRinging() {
        if (volume.available) {
            savedVolume = volume.volume
            savedMuted = volume.muted
            if (volume.muted)
                volume.muted = false
            if (volume.volume < 0.5)
                volume.volume = 0.5
        }
        player.stop()
        player.play()
        swell.restart()
        buzz.start()
        giveUp.restart()
    }
    function stopRinging() {
        player.stop()
        swell.stop()
        buzz.stop()
        giveUp.stop()
        if (savedVolume >= 0 && volume.available) {
            volume.volume = savedVolume
            volume.muted = savedMuted
        }
        savedVolume = -1
    }
    Connections {
        target: screen.alarms
        function onRingingChanged() {
            if (screen.alarms.ringing) {
                if (!player.playing)
                    screen.startRinging()
                else
                    giveUp.restart()    // the next one in line: its own 10 minutes
            } else {
                screen.stopRinging()
            }
        }
    }

    MediaPlayer {
        id: player
        source: "qrc:/qt/qml/Mobarchy/Shell/data/sounds/alarm.wav"
        loops: MediaPlayer.Infinite
        audioOutput: AudioOutput { id: out; volume: 0.15 }
    }
    // from soft to full in half a minute
    NumberAnimation {
        id: swell
        target: out; property: "volume"; from: 0.15; to: 1.0; duration: 30000
    }
    Timer {
        id: buzz
        interval: 1600
        repeat: true
        triggeredOnStart: true
        onTriggered: screen.haptics.buzz()
    }
    Timer {
        id: giveUp
        interval: 10 * 60 * 1000
        onTriggered: screen.alarms.dismiss()
    }

    // --- what rings, and the buttons ----------------------------------------------------
    property date now: new Date()
    Timer {
        interval: 1000
        running: screen.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: screen.now = new Date()
    }

    Column {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: parent.height * 0.16 }
        width: parent.width - 48
        spacing: 10
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: screen.isTimer ? "󰔛" : "󰀠"
            color: Theme.accent
            font { family: Theme.font; pixelSize: 56 }
            SequentialAnimation on scale {
                running: screen.visible
                loops: Animation.Infinite
                NumberAnimation { to: 1.12; duration: 500; easing.type: Easing.OutQuad }
                NumberAnimation { to: 1.0; duration: 700; easing.type: Easing.InOutQuad }
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: Qt.formatTime(screen.now, "HH:mm")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 88; weight: Font.Light }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: screen.alarms.label !== "" ? screen.alarms.label
                                             : (screen.isTimer ? qsTr("Time's up") : qsTr("Alarm"))
            color: Theme.fg
            font { family: Theme.font; pixelSize: 24 }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !screen.isTimer
            text: qsTr("Set for %1").arg(Qt.formatTime(screen.alarms.ringTime, "HH:mm"))
            color: Theme.muted
            font { family: Theme.font; pixelSize: 15 }
        }
    }

    Column {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: parent.height * 0.12 }
        width: Math.min(parent.width - 64, 420)
        spacing: 18
        Rectangle {
            visible: !screen.isTimer
            width: parent.width; height: 64; radius: 32
            color: snoozeTap.pressed ? Theme.line : Theme.panel
            border.color: Theme.line
            Text {
                anchors.centerIn: parent
                text: "󰒲  " + qsTr("Snooze %1 min").arg(screen.alarms.snoozeMinutes)
                color: Theme.fg
                font { family: Theme.font; pixelSize: 18 }
            }
            MouseArea {
                id: snoozeTap
                anchors.fill: parent
                onClicked: { screen.haptics.click(); screen.alarms.snooze() }
            }
        }
        Rectangle {
            width: parent.width; height: 64; radius: 32
            color: stopTap.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent
            Text {
                anchors.centerIn: parent
                text: "󰓛  " + qsTr("Stop")
                color: Theme.bg
                font { family: Theme.font; pixelSize: 18; bold: true }
            }
            MouseArea {
                id: stopTap
                anchors.fill: parent
                onClicked: { screen.haptics.click(); screen.alarms.dismiss() }
            }
        }
    }
}
