pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The calendar (ADR 0034), in the manner of Notion Calendar: a week strip, the day as a
// timeline with the events as soft blocks in their calendars' colours, a line for now, the
// month at a glance, and a sheet for a new event or one of the phone's. The accounts'
// calendars (iCloud, Google, ...) are read only for now. Its own window
// (mobarchy-shell --calendar); the events are mobarchy-calendar's (CalendarData).
Window {
    id: win

    visible: true
    title: qsTr("Calendar")
    color: Theme.bg

    property date day: new Date()               // the day shown
    property bool monthView: false
    property var editing: null                  // the event in the sheet ({} for a new one)
    property real now: Date.now()
    Timer { interval: 30000; running: true; repeat: true; onTriggered: win.now = Date.now() }

    readonly property real hourHeight: 64
    readonly property int firstDay: 1           // Monday

    function dayOnly(d) { return new Date(d.getFullYear(), d.getMonth(), d.getDate()) }
    function addDays(d, n) { return new Date(d.getFullYear(), d.getMonth(), d.getDate() + n) }
    function sameDay(a, b) { return a.toDateString() === b.toDateString() }
    function iso(d) { return Qt.formatDate(d, "yyyy-MM-dd") }
    function weekStart(d) { return addDays(dayOnly(d), -((d.getDay() - firstDay + 7) % 7)) }
    function monthStart(d) { return new Date(d.getFullYear(), d.getMonth(), 1) }
    function time(s) { return Qt.formatTime(new Date(s), "HH:mm") }

    // the data: the shown day's, or the shown month's (the weeks it touches)
    CalendarData {
        id: data
        from: win.monthView ? win.weekStart(win.monthStart(win.day)) : win.dayOnly(win.day)
        days: win.monthView ? 42 : 1
    }
    // the week strip's dots: the week's events
    CalendarData {
        id: week
        from: win.weekStart(win.day)
        days: 7
    }

    function eventsOn(list, d) {
        const start = dayOnly(d).getTime(), end = addDays(d, 1).getTime()
        return list.filter(e => new Date(e.start).getTime() < end && new Date(e.end).getTime() > start
                                || e.all_day && sameDay(new Date(e.start), d))
    }

    // --- the header: the month, today, day | month ------------------------------------------
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 76
        Text {
            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
            text: win.day.toLocaleDateString(Qt.locale(), "MMMM")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 26; bold: true; capitalization: Font.Capitalize }
            Text {
                anchors { left: parent.right; leftMargin: 8; baseline: parent.baseline }
                text: win.day.getFullYear()
                color: Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
        }
        Row {
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 8
            component Pill: Rectangle {
                id: pill
                property string label
                property bool on: false
                signal clicked()
                width: pillText.implicitWidth + 26
                height: 34
                radius: 17
                color: on ? Theme.fg : (pillTap.pressed ? Theme.line : "transparent")
                border { color: on ? Theme.fg : Qt.alpha(Theme.fg, 0.2); width: 1 }
                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: pill.label
                    color: pill.on ? Theme.bg : Theme.fg
                    font { family: Theme.font; pixelSize: 13; bold: pill.on }
                }
                MouseArea { id: pillTap; anchors.fill: parent; anchors.margins: -4; onClicked: pill.clicked() }
            }
            Pill {
                label: qsTr("Today")
                visible: !win.sameDay(win.day, new Date())
                onClicked: win.day = new Date()
            }
            Pill { label: qsTr("Day"); on: !win.monthView; onClicked: win.monthView = false }
            Pill { label: qsTr("Month"); on: win.monthView; onClicked: win.monthView = true }
        }
    }

    // --- the week strip -------------------------------------------------------------------
    Row {
        id: strip
        visible: !win.monthView
        anchors { left: parent.left; right: parent.right; top: header.bottom; leftMargin: 10; rightMargin: 10 }
        height: 74
        Repeater {
            model: 7
            delegate: Item {
                id: cell
                required property int index
                readonly property date d: win.addDays(win.weekStart(win.day), index)
                readonly property bool chosen: win.sameDay(d, win.day)
                readonly property bool today: win.sameDay(d, new Date(win.now))
                width: strip.width / 7
                height: strip.height
                Column {
                    anchors.centerIn: parent
                    spacing: 6
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: cell.d.toLocaleDateString(Qt.locale(), "ddd").slice(0, 2)
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 12; capitalization: Font.Capitalize }
                    }
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 36; height: 36; radius: 18
                        color: cell.chosen ? (cell.today ? Theme.red : Theme.fg) : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: cell.d.getDate()
                            color: cell.chosen ? Theme.bg : cell.today ? Theme.red : Theme.fg
                            font { family: Theme.font; pixelSize: 15; bold: cell.chosen || cell.today }
                        }
                    }
                    // a dot per calendar with something that day (room for them either way)
                    Item {
                      width: 30; height: 5
                      anchors.horizontalCenter: parent.horizontalCenter
                      Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 3
                        Repeater {
                            model: [...new Set(win.eventsOn(week.events, cell.d).map(e => e.color))].slice(0, 3)
                            delegate: Rectangle {
                                required property var modelData
                                width: 5; height: 5; radius: 2.5
                                color: modelData
                            }
                        }
                      }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: win.day = cell.d }
            }
        }
        // swiping the strip: a week on or back
        DragHandler {
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: if (!active && Math.abs(translation.x) > 60)
                                 win.day = win.addDays(win.day, translation.x < 0 ? 7 : -7)
        }
    }

    // --- the day: all-day events, then the timeline -------------------------------------------
    Item {
        id: dayView
        visible: !win.monthView
        anchors { left: parent.left; right: parent.right; top: strip.bottom; bottom: parent.bottom }
        readonly property var today: win.eventsOn(data.events, win.day)
        readonly property var allDay: today.filter(e => e.all_day)
        readonly property var timed: today.filter(e => !e.all_day)

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 1
            color: Theme.line
        }
        Column {
            id: allDayColumn
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 12; leftMargin: 64 }
            spacing: 4
            topPadding: allDayRepeater.count > 0 ? 8 : 0
            Repeater {
                id: allDayRepeater
                model: dayView.allDay
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    width: allDayColumn.width
                    height: 30
                    radius: 8
                    color: Qt.alpha(modelData.color, 0.22)
                    Text {
                        anchors { left: parent.left; leftMargin: 10; right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                        elide: Text.ElideRight
                        text: chip.modelData.title
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 13; bold: true }
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.editing = chip.modelData }
                }
            }
        }

        Flickable {
            id: timeline
            anchors { left: parent.left; right: parent.right; top: allDayColumn.bottom; bottom: parent.bottom
                      topMargin: 6 }
            clip: true
            contentHeight: 24 * win.hourHeight + 40
            boundsBehavior: Theme.boundsBehavior
            maximumFlickVelocity: Theme.flickVelocity
            flickDeceleration: Theme.flickDeceleration
            Component.onCompleted: contentY = Math.max(0, (new Date().getHours() - 1.5) * win.hourHeight)

            // the hours
            Repeater {
                model: 24
                delegate: Item {
                    required property int index
                    y: index * win.hourHeight + 12
                    width: timeline.width
                    height: win.hourHeight
                    Text {
                        anchors { right: parent.left; rightMargin: -48; verticalCenter: parent.top }
                        visible: index > 0
                        text: (index < 10 ? "0" : "") + index + ":00"
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 11 }
                    }
                    Rectangle {
                        x: 58; width: parent.width - 70; height: 1
                        visible: index > 0
                        color: Qt.alpha(Theme.fg, 0.07)
                    }
                    // a tap on an empty hour: a new event then
                    MouseArea {
                        x: 58; width: parent.width - 70; height: parent.height
                        onClicked: {
                            const s = new Date(win.day.getFullYear(), win.day.getMonth(), win.day.getDate(), index)
                            win.editing = { start: s.toISOString(), end: new Date(s.getTime() + 3600000).toISOString(),
                                            all_day: false, isNew: true }
                        }
                    }
                }
            }

            // the events: side by side where they overlap
            Repeater {
                model: {
                    const evs = dayView.timed.slice().sort((a, b) => a.start.localeCompare(b.start))
                    const lanes = []    // each lane's end
                    const placed = []
                    const dayStart = win.dayOnly(win.day).getTime()
                    for (const e of evs) {
                        const s = Math.max(new Date(e.start).getTime(), dayStart)
                        const en = Math.min(new Date(e.end).getTime(), dayStart + 86400000)
                        let lane = lanes.findIndex(l => l <= s)
                        if (lane < 0) { lane = lanes.length; lanes.push(en) } else lanes[lane] = en
                        placed.push({ ev: e, top: (s - dayStart) / 3600000, hours: Math.max(0.4, (en - s) / 3600000), lane: lane })
                    }
                    return placed.map(p => Object.assign(p, { lanes: lanes.length }))
                }
                delegate: Rectangle {
                    id: block
                    required property var modelData
                    readonly property var ev: modelData.ev
                    readonly property real w: (timeline.width - 74) / modelData.lanes
                    x: 62 + modelData.lane * w
                    y: 12 + modelData.top * win.hourHeight + 1
                    width: w - 4
                    height: modelData.hours * win.hourHeight - 3
                    radius: 8
                    color: Qt.alpha(ev.color, blockTap.pressed ? 0.34 : 0.22)
                    clip: true
                    Rectangle {     // its calendar's colour, at its edge
                        width: 4; height: parent.height; radius: 2
                        color: block.ev.color
                    }
                    Column {
                        anchors { left: parent.left; right: parent.right; top: parent.top
                                  leftMargin: 12; rightMargin: 8; topMargin: 6 }
                        spacing: 1
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: block.ev.title
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 13; bold: true }
                        }
                        Text {
                            visible: block.height > 40
                            width: parent.width
                            elide: Text.ElideRight
                            text: win.time(block.ev.start) + " – " + win.time(block.ev.end)
                                  + (block.ev.location ? "  ·  " + block.ev.location : "")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 11 }
                        }
                    }
                    MouseArea { id: blockTap; anchors.fill: parent; onClicked: win.editing = block.ev }
                }
            }

            // now
            Item {
                visible: win.sameDay(win.day, new Date(win.now))
                readonly property real at: (win.now - win.dayOnly(new Date(win.now)).getTime()) / 3600000
                y: 12 + at * win.hourHeight
                width: timeline.width
                Rectangle { x: 54; y: -4; width: 9; height: 9; radius: 4.5; color: Theme.red }
                Rectangle { x: 58; width: parent.width - 70; height: 2; y: -1; color: Theme.red }
            }
        }

        // swiping the day: a day on or back
        DragHandler {
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: if (!active && Math.abs(translation.x) > 80)
                                 win.day = win.addDays(win.day, translation.x < 0 ? 1 : -1)
        }
    }

    // --- the month ---------------------------------------------------------------------------
    Item {
        id: monthView
        visible: win.monthView
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom
                  leftMargin: 8; rightMargin: 8 }
        Row {
            id: weekdays
            width: parent.width
            Repeater {
                model: 7
                delegate: Text {
                    required property int index
                    width: weekdays.width / 7
                    horizontalAlignment: Text.AlignHCenter
                    text: win.addDays(win.weekStart(new Date()), index).toLocaleDateString(Qt.locale(), "ddd").slice(0, 2)
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 12; capitalization: Font.Capitalize }
                }
            }
        }
        Grid {
            id: grid
            anchors { top: weekdays.bottom; topMargin: 10 }
            width: parent.width
            columns: 7
            Repeater {
                model: 42
                delegate: Rectangle {
                    id: mcell
                    required property int index
                    readonly property date d: win.addDays(win.weekStart(win.monthStart(win.day)), index)
                    readonly property bool inMonth: d.getMonth() === win.day.getMonth()
                    readonly property bool today: win.sameDay(d, new Date(win.now))
                    readonly property var evs: win.eventsOn(data.events, d)
                    width: grid.width / 7
                    height: Math.min(96, (monthView.height - 40) / 6)
                    color: "transparent"
                    border { color: Qt.alpha(Theme.fg, 0.06); width: 1 }
                    Rectangle {
                        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 4 }
                        width: 26; height: 26; radius: 13
                        color: mcell.today ? Theme.red : "transparent"
                        Text {
                            anchors.centerIn: parent
                            text: mcell.d.getDate()
                            color: mcell.today ? Theme.bg : mcell.inMonth ? Theme.fg : Qt.alpha(Theme.muted, 0.6)
                            font { family: Theme.font; pixelSize: 13; bold: mcell.today }
                        }
                    }
                    Column {
                        anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 33; margins: 3 }
                        spacing: 2
                        Repeater {
                            model: mcell.evs.slice(0, 3)
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 14
                                radius: 3
                                color: Qt.alpha(modelData.color, 0.28)
                                Text {
                                    anchors { fill: parent; leftMargin: 3 }
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    text: parent.modelData.title
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 9 }
                                }
                            }
                        }
                        Text {
                            visible: mcell.evs.length > 3
                            text: "+" + (mcell.evs.length - 3)
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 9 }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: { win.day = mcell.d; win.monthView = false }
                    }
                }
            }
        }
        DragHandler {
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: if (!active && Math.abs(translation.x) > 80)
                                 win.day = new Date(win.day.getFullYear(), win.day.getMonth() + (translation.x < 0 ? 1 : -1), 1)
        }
    }

    // --- a new event -------------------------------------------------------------------------
    Rectangle {
        anchors { right: parent.right; bottom: parent.bottom; margins: 22 }
        visible: win.editing === null
        width: 58; height: 58; radius: 29
        color: addTap.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent
        Text {
            anchors.centerIn: parent
            text: "󰐕"
            color: Theme.bg
            font { family: Theme.font; pixelSize: 26 }
        }
        MouseArea {
            id: addTap
            anchors.fill: parent
            onClicked: {
                const n = new Date()
                const s = new Date(win.day.getFullYear(), win.day.getMonth(), win.day.getDate(), Math.min(23, n.getHours() + 1))
                win.editing = { start: s.toISOString(), end: new Date(s.getTime() + 3600000).toISOString(),
                                all_day: false, isNew: true }
            }
        }
    }

    Text {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 30 }
        visible: data.error !== ""
        text: data.error
        color: Theme.red
        font { family: Theme.font; pixelSize: 13 }
    }

    // --- the event's sheet ---------------------------------------------------------------------
    EventSheet {
        anchors.fill: parent
        visible: win.editing !== null
        event: win.editing
        store: data
        onDone: win.editing = null
    }
}
