import QtQuick
import Mobarchy.Shell

// The next alarm, or a running timer counting down (a timer goes first: it is sooner). Only
// when there is one; tapping it opens the Clock.
BoardCard {
    id: card

    required property Alarms alarms
    required property AppModel apps

    readonly property var timer: alarms.timerList.length > 0 ? alarms.timerList[0] : null
    readonly property bool hasAlarm: !isNaN(alarms.nextAlarm.getTime())
    property real now: Date.now()

    wanted: timer !== null || hasAlarm
    title: timer ? (timer.label || qsTr("Timer")) : qsTr("Alarm")
    icon: timer ? "󰔛" : "󰀠"
    onTapped: apps.launch("org.mobarchy.Clock")

    Timer {
        interval: 1000
        running: card.visible && card.timer !== null
        repeat: true
        onTriggered: card.now = Date.now()
    }

    function remaining(ends) {
        const s = Math.max(0, Math.round((new Date(ends).getTime() - now) / 1000))
        const h = Math.floor(s / 3600), m = Math.floor(s % 3600 / 60)
        return (h > 0 ? h + ":" + String(m).padStart(2, "0") : m) + ":" + String(s % 60).padStart(2, "0")
    }
    function day(at) {
        const today = new Date()
        const tomorrow = new Date(today.getFullYear(), today.getMonth(), today.getDate() + 1)
        if (at.toDateString() === today.toDateString()) return qsTr("Today")
        if (at.toDateString() === tomorrow.toDateString()) return qsTr("Tomorrow")
        return at.toLocaleDateString(Qt.locale(), "dddd")
    }

    Text {
        anchors { left: parent.left; top: parent.top; topMargin: -2 }
        text: card.timer ? card.remaining(card.timer.ends)
                         : card.hasAlarm ? card.alarms.nextAlarm.toLocaleTimeString(Qt.locale(), "HH:mm") : ""
        color: Theme.fg
        font { family: Theme.font; pixelSize: 40; weight: Font.Light }
    }
    Text {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        elide: Text.ElideRight
        text: card.timer ? qsTr("left") : card.hasAlarm ? card.day(card.alarms.nextAlarm) : ""
        color: Theme.muted
        font { family: Theme.font; pixelSize: 13 }
    }
}
