pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The calendar (ADR 0034), in the manner of Notion Calendar: a week strip, the day as a
// timeline with the events as soft blocks in their calendars' colours, a line for now, the
// week as seven such columns side by side, the month at a glance, and a sheet for a new event or one of the phone's. The accounts'
// calendars (iCloud, Google, ...) are read only for now. Its own window
// (mobarchy-shell --calendar); the events are mobarchy-calendar's (CalendarData).
Window {
    id: win

    visible: true
    title: qsTr("Calendar")
    color: Theme.bg

    property date day: new Date()               // the day shown
    property bool monthView: false
    property bool weekView: false
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

    // a day's timed events laid out: [{ev, top, hours, lane, lanes}], side by side where they
    // overlap
    function layout(evs, d) {
        evs = evs.filter(e => !e.all_day).sort((a, b) => a.start.localeCompare(b.start))
        const lanes = []    // each lane's end
        const placed = []
        const dayStart = dayOnly(d).getTime()
        for (const e of evs) {
            const s = Math.max(new Date(e.start).getTime(), dayStart)
            const en = Math.min(new Date(e.end).getTime(), dayStart + 86400000)
            let lane = lanes.findIndex(l => l <= s)
            if (lane < 0) { lane = lanes.length; lanes.push(en) } else lanes[lane] = en
            placed.push({ ev: e, top: (s - dayStart) / 3600000, hours: Math.max(0.4, (en - s) / 3600000), lane: lane })
        }
        return placed.map(p => Object.assign(p, { lanes: lanes.length }))
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
            id: monthName
            anchors { left: parent.left; leftMargin: 22; verticalCenter: parent.verticalCenter }
            text: win.day.toLocaleDateString(Qt.locale(), "MMMM")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 26; bold: true; capitalization: Font.Capitalize }
            Text {
                anchors { left: parent.right; leftMargin: 8; baseline: parent.baseline }
                text: win.day.getFullYear()
                // (only where the buttons leave room for it)
                visible: monthName.x + monthName.width + 8 + implicitWidth + 12 < pills.x
                color: Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
        }
        Row {
            id: pills
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
                // (in the week: unless today is among the days in sight)
                visible: win.weekView ? !weekView.inSight(new Date(win.now))
                                      : !win.sameDay(win.day, new Date(win.now))
                onClicked: win.day = new Date()
            }
            Pill { label: qsTr("Day"); on: !win.monthView && !win.weekView
                   onClicked: { win.monthView = false; win.weekView = false } }
            Pill { label: qsTr("Week"); on: win.weekView; onClicked: { win.monthView = false; win.weekView = true } }
            Pill { label: qsTr("Month"); on: win.monthView; onClicked: { win.weekView = false; win.monthView = true } }
        }
    }

    // --- the week strip -------------------------------------------------------------------
    Row {
        id: strip
        visible: !win.monthView && !win.weekView
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
        visible: !win.monthView && !win.weekView
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
                model: win.layout(dayView.timed, win.day)
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

    // --- the week: seven days in view, on a band of days that scrolls sideways ---------------
    // The days lie on a band from a year back to a year on (weekView.base); a drag sideways
    // moves it as far as it is flung, and it stops where it stops. The hours stay put at
    // the left; the days' names, their all-day events and their hours move together. Only the
    // days in sight (and one either side) are made; their events come from `range`: six
    // weeks around them, asked for again only at rest, near its edges (each question is a
    // process: one per day flung past brought the phone to its knees).
    CalendarData {
        id: range
        from: win.addDays(weekView.base, weekView.loaded)
        days: 42
    }
    Item {
        id: weekView
        visible: win.weekView
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        readonly property real gutter: 44
        readonly property real col: (width - gutter - 8) / 7
        readonly property int span: 731
        // day 0 of the band: a year before this week (fixed while the view is up)
        property date base: win.addDays(win.weekStart(new Date()), -365)
        // the first day in view, whole
        readonly property int first: Math.max(0, Math.floor(band.contentX / Math.max(1, col)))
        function index(d) { return Math.round((win.dayOnly(d) - base) / 86400000) }
        function dayAt(i) { return win.addDays(base, i) }
        // the first day of `range`'s six weeks
        property int loaded: index(win.weekStart(new Date())) - 14
        function load(i) {
            if (i < loaded + 7 || i + 7 > loaded + 35)
                loaded = Math.max(0, i - 14)
        }
        function inSight(d) { const i = index(d); return i >= first && i < first + 7 }
        // the band to a day (the week's first, as "Today" and the other views ask)
        property bool scrolling: false
        function show(d, animate) {
            const x = Math.max(0, Math.min(span - 7, index(win.weekStart(d)))) * col
            load(Math.round(x / col))
            if (animate) { settle.to = x; settle.restart() } else band.contentX = x
        }
        onColChanged: if (visible) band.contentX = first * col
        onVisibleChanged: if (visible) show(win.day, false)
        Connections {
            target: win
            // (another view, or "Today": the band there, unless that day is in view already)
            function onDayChanged() {
                if (!weekView.scrolling && weekView.visible) {
                    const i = weekView.index(win.day)
                    if (!weekView.inSight(win.day))
                        weekView.show(win.day, true)
                }
            }
        }
        NumberAnimation { id: settle; target: band; property: "contentX"; duration: 260; easing.type: Easing.OutCubic }

        // the band: sideways only; the days' names, all-day events and hours on it
        Flickable {
            id: band
            x: weekView.gutter
            width: weekView.width - weekView.gutter - 8
            height: weekView.height
            clip: true
            flickableDirection: Flickable.HorizontalFlick
            contentWidth: weekView.span * weekView.col
            contentHeight: height
            boundsBehavior: Flickable.StopAtBounds
            maximumFlickVelocity: Theme.flickVelocity
            flickDeceleration: Theme.flickDeceleration
            onMovementStarted: weekView.scrolling = true
            // at rest: the day mostly in sight at the left is the one shown (Day and the
            // header follow), and the events around it are asked for
            onMovementEnded: {
                const i = Math.round(contentX / weekView.col)
                win.day = weekView.dayAt(i)
                weekView.load(i)
                weekView.scrolling = false
            }

            // the days in sight, and one either side
            readonly property int from: Math.max(0, Math.min(weekView.span - count, weekView.first - 1))
            readonly property int count: 9
            // the day a slot shows: each slot keeps its day while the band moves, and only
            // the one that falls out of sight takes the day coming in (one column made anew
            // per day passed, not nine)
            function slotDay(k) { return from + ((k - from) % count + count) % count }

            // their names and dates (a tap: that day, in Day)
            Repeater {
                model: band.count
                delegate: Item {
                    id: wday
                    required property int index
                    readonly property int day: band.slotDay(index)
                    readonly property date d: weekView.dayAt(day)
                    readonly property bool today: win.sameDay(d, new Date(win.now))
                    x: day * weekView.col
                    width: weekView.col; height: 52
                    Column {
                        anchors.centerIn: parent
                        spacing: 3
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: wday.d.toLocaleDateString(Qt.locale(), "ddd").slice(0, 2)
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 11; capitalization: Font.Capitalize }
                        }
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 28; height: 28; radius: 14
                            color: wday.today ? Theme.red : "transparent"
                            Text {
                                anchors.centerIn: parent
                                text: wday.d.getDate()
                                color: wday.today ? Theme.bg : Theme.fg
                                font { family: Theme.font; pixelSize: 14; bold: wday.today }
                            }
                        }
                    }
                    MouseArea { anchors.fill: parent; onClicked: { win.day = wday.d; win.weekView = false } }
                }
            }
            // the all-day ones, under the days
            Item {
                id: weekAllDay
                y: 52
                width: band.contentWidth
                // (as tall as the most all-day events on a day in sight)
                height: {
                    let n = 0
                    for (let i = 0; i < band.count; ++i)
                        n = Math.max(n, win.eventsOn(range.events, weekView.dayAt(band.from + i)).filter(e => e.all_day).length)
                    return n * 22 + 4
                }
                Repeater {
                    model: band.count
                    delegate: Column {
                        id: adCol
                        required property int index
                        readonly property int day: band.slotDay(index)
                        readonly property var evs: win.eventsOn(range.events, weekView.dayAt(day)).filter(e => e.all_day)
                        x: day * weekView.col
                        width: weekView.col
                        spacing: 2
                        Repeater {
                            model: adCol.evs
                            delegate: Rectangle {
                                id: adChip
                                required property var modelData
                                width: weekView.col - 3; height: 20; radius: 5
                                color: Qt.alpha(modelData.color, 0.28)
                                Text {
                                    anchors { fill: parent; leftMargin: 4; rightMargin: 2 }
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                    text: adChip.modelData.title
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 10; bold: true }
                                }
                                MouseArea { anchors.fill: parent; onClicked: win.editing = adChip.modelData }
                            }
                        }
                    }
                }
            }

            // the hours: up and down, inside the band
            Flickable {
                id: weekTimeline
                y: weekAllDay.y + weekAllDay.height + 1
                width: band.contentWidth
                height: band.height - y
                clip: true
                flickableDirection: Flickable.VerticalFlick
                contentHeight: 24 * win.hourHeight + 40
                boundsBehavior: Theme.boundsBehavior
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration
                Component.onCompleted: contentY = Math.max(0, (new Date().getHours() - 1.5) * win.hourHeight)

                // the hours' lines (as wide as the days in sight), and the days' lines
                Repeater {
                    model: 23
                    delegate: Rectangle {
                        required property int index
                        x: band.from * weekView.col
                        y: (index + 1) * win.hourHeight + 12
                        width: band.count * weekView.col; height: 1
                        color: Qt.alpha(Theme.fg, 0.07)
                    }
                }
                Repeater {
                    model: band.count
                    delegate: Rectangle {
                        required property int index
                        x: band.slotDay(index) * weekView.col
                        y: 12
                        width: 1; height: 24 * win.hourHeight
                        color: Qt.alpha(Theme.fg, 0.05)
                    }
                }
                // a tap on an empty hour of a day: a new event then
                MouseArea {
                    x: band.from * weekView.col; y: 12
                    width: band.count * weekView.col; height: 24 * win.hourHeight
                    onClicked: (m) => {
                        const d = weekView.dayAt(band.from + Math.floor(m.x / weekView.col))
                        const s = new Date(d.getFullYear(), d.getMonth(), d.getDate(), Math.floor(m.y / win.hourHeight))
                        win.editing = { start: s.toISOString(), end: new Date(s.getTime() + 3600000).toISOString(),
                                        all_day: false, isNew: true }
                    }
                }
                // each day's events, in its column
                Repeater {
                    model: band.count
                    delegate: Item {
                        id: wcol
                        required property int index
                        readonly property int day: band.slotDay(index)
                        readonly property date d: weekView.dayAt(day)
                        x: day * weekView.col
                        y: 12
                        width: weekView.col
                        height: 24 * win.hourHeight
                        Repeater {
                            model: win.layout(win.eventsOn(range.events, wcol.d), wcol.d)
                            delegate: Rectangle {
                                id: wblock
                                required property var modelData
                                readonly property var ev: modelData.ev
                                readonly property real w: (wcol.width - 2) / modelData.lanes
                                x: 1 + modelData.lane * w
                                y: modelData.top * win.hourHeight + 1
                                width: w - 2
                                height: Math.max(16, modelData.hours * win.hourHeight - 2)
                                radius: 5
                                color: Qt.alpha(ev.color, wTap.pressed ? 0.4 : 0.26)
                                clip: true
                                Rectangle { width: 3; height: parent.height; color: wblock.ev.color }
                                Text {
                                    anchors { left: parent.left; right: parent.right; top: parent.top
                                              leftMargin: 5; rightMargin: 2; topMargin: 2 }
                                    wrapMode: Text.Wrap
                                    maximumLineCount: Math.max(1, Math.floor((wblock.height - 4) / 13))
                                    elide: Text.ElideRight
                                    text: wblock.ev.title
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 10; bold: true }
                                }
                                MouseArea { id: wTap; anchors.fill: parent; onClicked: win.editing = wblock.ev }
                            }
                        }
                    }
                }
                // now
                Item {
                    readonly property int day: weekView.index(new Date(win.now))
                    readonly property real at: (win.now - win.dayOnly(new Date(win.now)).getTime()) / 3600000
                    x: day * weekView.col
                    y: 12 + at * win.hourHeight
                    width: weekView.col
                    Rectangle { x: -4; y: -4; width: 8; height: 8; radius: 4; color: Theme.red }
                    Rectangle { width: parent.width; height: 2; y: -1; color: Theme.red }
                }
            }
        }

        // the hours' names: put at the left, moving up and down with the days
        Item {
            x: 0; y: weekTimeline.y
            width: weekView.gutter; height: weekTimeline.height
            clip: true
            Repeater {
                model: 23
                delegate: Text {
                    required property int index
                    x: 4
                    y: (index + 1) * win.hourHeight + 12 - weekTimeline.contentY - height / 2
                    text: (index + 1 < 10 ? "0" : "") + (index + 1)
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 10 }
                }
            }
        }
        Rectangle {
            x: 0; y: weekTimeline.y - 1
            width: weekView.width; height: 1
            color: Theme.line
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
