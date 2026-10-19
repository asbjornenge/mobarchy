import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

LayerWindow {
    id: bar

    required property SystemStatus system
    required property Sheet quick
    required property Device device
    required property Notifications notifications
    required property Telephony telephony
    property bool flight: false
    // the microphone is on for Frigg (waiting for "Hei Frigg", or talking)
    property bool listening: false
    // an alarm is set (Alarms.nextAlarm valid)
    property bool alarmSet: false

    // The bar starts at the very top edge, where a swipe starts, and covers the strip the
    // device keeps clear (camera cutout, corners: device.toml top_inset). Its content sits
    // below that strip.
    // how the screen is turned (Rotation.turns): the strip is at the panel's top only upright
    property int turns: 0
    readonly property int inset: device.inset(screen ? screen.name : "", "top", turns)
    height: inset + 28
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-status"
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: height
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    property date now: new Date()
    Timer {
        // Wakes up once a minute, on the minute.
        interval: (60 - bar.now.getSeconds()) * 1000 + 50
        running: true
        onTriggered: { bar.now = new Date(); restart() }
    }

    readonly property string batteryIcon: {
        if (system.battery < 0) return ""
        if (system.charging) return "󰂄"
        const icons = ["󰂎", "󰁺", "󰁻", "󰁼", "󰁽", "󰁾", "󰁿", "󰂀", "󰂁", "󰂂", "󰁹"]
        return icons[Math.round(system.battery / 10)]
    }
    // the mobile network: its signal (and "4G" etc. on mobile data), "No SIM", "No service"
    // (nothing when there is no modem)
    readonly property string cellText: {
        const t = telephony
        if (bar.flight) return "󰀝"
        if (!t.available) return ""
        if (!t.radio) return ""
        if (!t.simPresent) return "No SIM"
        if (t.pinRequired !== "none") return "SIM locked"
        if (t.registration !== "registered" && t.registration !== "roaming")
            return t.registration === "searching" ? "Searching" : "No service"
        // the signal as bars; the network's kind beside them while the internet goes over
        // it (mobile data up, no Wi-Fi)
        const bars = ["\u{F08BF}", "\u{F08BC}", "\u{F08BD}", "\u{F08BE}"][Math.min(3, Math.ceil(t.signalStrength / 25))]
        const tech = ({ nr: "5G", lte: "4G", umts: "3G", hspa: "3G", hsdpa: "3G", hsupa: "3G",
                        edge: "E", gprs: "G", gsm: "2G" })[t.technology] ?? ""
        return bars + (t.dataActive && !system.wifiConnected && tech ? " " + tech : "")
               + (t.registration === "roaming" ? " R" : "")
    }
    readonly property string wifiIcon: !system.wifiConnected ? "󰤮"
        : system.wifiStrength < 0 ? "󰤨" : ["󰤯", "󰤟", "󰤢", "󰤥", "󰤨"][system.wifiStrength]

    Item {
        id: row
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 28
    }
    Row {
        anchors { left: parent.left; leftMargin: 16; verticalCenter: row.verticalCenter }
        spacing: 12
        Text {
            text: Qt.formatTime(bar.now, "HH:mm")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 14; bold: true }
        }
        Text {
            visible: bar.notifications.count > 0
            text: "󰂚 " + bar.notifications.count
            color: Theme.accent
            font { family: Theme.font; pixelSize: 13 }
        }
    }
    Row {
        anchors { right: parent.right; rightMargin: 16; verticalCenter: row.verticalCenter }
        spacing: 9
        Repeater {
            model: [bar.listening ? "󰍬" : "", bar.alarmSet ? "󰀠" : "", bar.cellText, bar.wifiIcon,
                    bar.batteryIcon + (bar.system.battery >= 0 ? " " + bar.system.battery + "%" : "")]
                   .filter(t => t !== "")
            delegate: Text {
                required property string modelData
                text: modelData
                color: Theme.fg
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }

    // Swipe down (or tap) for quick settings.
    EdgeGesture {
        anchors.fill: parent
        direction: -1
        onBegan: bar.quick.beginDrag()
        onMoved: (distance) => bar.quick.dragTo(distance)
        onEnded: (velocity) => bar.quick.endDrag(velocity)
        onTapped: bar.quick.open()
    }
}
