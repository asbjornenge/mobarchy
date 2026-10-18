pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// What is coming, from the calendars (ADR 0034): the rest of today and tomorrow, each event
// with its calendar's colour; compact, the next one and when. Tapping opens the Calendar.
BoardCard {
    id: card

    required property AppModel apps

    CalendarData { id: data; days: 2 }
    property real now: Date.now()
    Timer {
        interval: 60000; running: card.live; repeat: true; triggeredOnStart: true
        onTriggered: {
            card.now = Date.now()
            data.from = new Date()      // (after midnight: from the new day)
        }
    }

    // what is still to come (ongoing ones too), all-day ones of today first
    readonly property var coming: data.events.filter(e => new Date(e.end).getTime() > now)
    readonly property var next: coming.find(e => !e.all_day) ?? coming[0] ?? null

    title: new Date(now).toLocaleDateString(Qt.locale(), "dddd d. MMMM")
    icon: "󰃭"
    onTapped: apps.launch("org.mobarchy.Calendar")

    function when(e) {
        const s = new Date(e.start)
        const today = new Date(now).toDateString() === s.toDateString()
        if (e.all_day)
            return today ? qsTr("Today") : qsTr("Tomorrow")
        const mins = Math.round((s.getTime() - now) / 60000)
        if (mins <= 0)
            return qsTr("Now")
        if (mins < 60)
            return qsTr("in %1 min").arg(mins)
        return (today ? "" : qsTr("Tomorrow") + " ") + Qt.formatTime(s, "HH:mm")
    }

    compactIcon: "󰃭"
    compactValue: next ? when(next) : qsTr("Free")
    compactCaption: next ? next.title : qsTr("Nothing more today")

    Column {
        anchors.fill: parent
        spacing: 7
        Repeater {
            model: card.coming.slice(0, card.rows >= 4 ? 7 : card.cols >= 4 ? 3 : 2)
            delegate: Row {
                id: row
                required property var modelData
                width: parent.width
                height: 32
                spacing: 10
                Rectangle {
                    width: 4; height: parent.height; radius: 2
                    color: row.modelData.color
                }
                Column {
                    width: parent.width - 14
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: row.modelData.title
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 13; bold: true }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: card.when(row.modelData) + (row.modelData.location ? "  ·  " + row.modelData.location : "")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 11 }
                    }
                }
            }
        }
        Text {
            visible: card.coming.length === 0
            text: qsTr("Nothing more today or tomorrow")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
    }
}
