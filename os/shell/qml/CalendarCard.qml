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
    // (begun today or before: an all-day one of several days too)
    function isToday(e) {
        const d = new Date(now)
        return new Date(e.start) < new Date(d.getFullYear(), d.getMonth(), d.getDate() + 1)
    }
    // the rows shown: today's events, then tomorrow's under a heading of their own
    readonly property var lines: {
        const room = rows >= 4 ? 7.5 : cols >= 4 ? 3.5 : 2.5     // (events; a heading is half of one)
        const out = []
        let used = 0
        for (const e of coming) {
            const heading = !isToday(e) && !out.some(r => r.heading)
            if (used + 1 + (heading ? 0.5 : 0) > room)
                break
            if (heading) {
                out.push({ heading: qsTr("Tomorrow") })
                used += 0.5
            }
            out.push(e)
            used += 1
        }
        return out
    }

    title: new Date(now).toLocaleDateString(Qt.locale(), "dddd d. MMMM")
    icon: "󰃭"
    onTapped: openApp("org.mobarchy.Calendar")

    function when(e, underHeading) {
        const s = new Date(e.start)
        const today = new Date(now).toDateString() === s.toDateString()
        if (e.all_day)
            return today ? qsTr("Today") : underHeading ? qsTr("All day") : qsTr("Tomorrow")
        const mins = Math.round((s.getTime() - now) / 60000)
        if (mins <= 0)
            return qsTr("Now")
        if (mins < 60)
            return qsTr("in %1 min").arg(mins)
        return (today || underHeading ? "" : qsTr("Tomorrow") + " ") + Qt.formatTime(s, "HH:mm")
    }

    compactIcon: "󰃭"
    compactValue: next ? when(next) : qsTr("Free")
    compactCaption: next ? next.title : qsTr("Nothing more today")

    Column {
        anchors.fill: parent
        spacing: 7
        Repeater {
            model: card.lines
            delegate: Row {
                id: row
                required property var modelData
                width: parent.width
                height: modelData.heading ? 16 : 32
                spacing: 10
                Text {
                    visible: !!row.modelData.heading
                    anchors.bottom: parent.bottom
                    text: row.modelData.heading ?? ""
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 11; bold: true; capitalization: Font.AllUppercase }
                }
                Rectangle {
                    visible: !row.modelData.heading
                    width: 4; height: parent.height; radius: 2
                    color: row.modelData.color ?? "transparent"
                }
                Column {
                    visible: !row.modelData.heading
                    width: parent.width - 14
                    anchors.verticalCenter: parent.verticalCenter
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: row.modelData.title ?? ""
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 13; bold: true }
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: card.when(row.modelData, true) + (row.modelData.location ? "  ·  " + row.modelData.location : "")
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
