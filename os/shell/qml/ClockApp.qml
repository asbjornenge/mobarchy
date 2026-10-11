pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Clock (ADR 0028): the alarms and the timers, as the agent's tools set them too (the same
// file). Alarms: each with a switch; a tap edits it (time on wheels, days, label), "+" adds
// one. Timers: the running ones counting down, quick ones and a time on wheels to start.
// The shell rings them; this one only shows and changes them. Its own window
// (mobarchy-shell --clock), like the Themes.
Window {
    id: win

    visible: true
    title: qsTr("Clock")
    color: Theme.bg

    Alarms { id: alarms; ringer: false }

    property string tab: "alarms"
    property date now: new Date()
    Timer {
        interval: 1000
        running: win.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: win.now = new Date()
    }

    readonly property var dayNames: [qsTr("Mon"), qsTr("Tue"), qsTr("Wed"), qsTr("Thu"), qsTr("Fri"), qsTr("Sat"), qsTr("Sun")]
    function daysText(days) {
        if (!days || days.length === 0)
            return qsTr("Once")
        if (days.length === 7)
            return qsTr("Every day")
        if (days.join() === "1,2,3,4,5")
            return qsTr("Weekdays")
        if (days.join() === "6,7")
            return qsTr("Weekend")
        return days.map(d => win.dayNames[d - 1]).join(" ")
    }
    function untilText(next) {
        if (!next || isNaN(next.getTime()))
            return ""
        const mins = Math.round((next - win.now) / 60000)
        if (mins < 60)
            return qsTr("in %1 min").arg(Math.max(1, mins))
        const h = Math.floor(mins / 60), m = mins % 60
        return h < 24 ? qsTr("in %1 h %2 min").arg(h).arg(m) : next.toLocaleString(Qt.locale(), "ddd HH:mm")
    }
    function clockText(seconds) {
        const s = Math.max(0, Math.round(seconds))
        const h = Math.floor(s / 3600), m = Math.floor(s / 60) % 60, sec = s % 60
        const two = n => (n < 10 ? "0" : "") + n
        return (h > 0 ? h + ":" + two(m) : m) + ":" + two(sec)
    }

    // --- the tabs ------------------------------------------------------------------------
    Row {
        id: tabs
        anchors { top: parent.top; topMargin: 24; horizontalCenter: parent.horizontalCenter }
        spacing: 8
        Repeater {
            model: [{key: "alarms", text: "󰀠  " + qsTr("Alarms")}, {key: "timers", text: "󰔛  " + qsTr("Timers")}]
            delegate: Rectangle {
                id: tabButton
                required property var modelData
                readonly property bool on: win.tab === modelData.key
                width: 150; height: 44; radius: 22
                color: on ? Theme.accent : Theme.panel
                Text {
                    anchors.centerIn: parent
                    text: tabButton.modelData.text
                    color: tabButton.on ? Theme.bg : Theme.fg
                    font { family: Theme.font; pixelSize: 15; bold: tabButton.on }
                }
                MouseArea { anchors.fill: parent; onClicked: win.tab = tabButton.modelData.key }
            }
        }
    }

    // --- alarms ------------------------------------------------------------------------------
    ListView {
        id: alarmList
        visible: win.tab === "alarms"
        anchors { top: tabs.bottom; topMargin: 20; left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 16; rightMargin: 16 }
        clip: true
        spacing: 12
        bottomMargin: 110
        boundsBehavior: Theme.boundsBehavior
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
        model: alarms.alarmList
        delegate: Rectangle {
            id: row
            required property var modelData
            width: alarmList.width
            height: 96
            radius: 20
            color: rowTap.pressed ? Theme.line : Theme.panel
            MouseArea { id: rowTap; anchors.fill: parent; onClicked: editor.edit(row.modelData) }
            Column {
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text {
                    text: row.modelData.time
                    color: row.modelData.enabled ? Theme.fg : Theme.muted
                    font { family: Theme.font; pixelSize: 38; weight: Font.Light }
                }
                Text {
                    text: [row.modelData.label, win.daysText(row.modelData.days),
                           row.modelData.enabled ? win.untilText(row.modelData.next) : ""].filter(s => s).join("  ·  ")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
            }
            Switch {
                anchors { right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                checked: row.modelData.enabled
                onToggled: alarms.setEnabled(row.modelData.id, !row.modelData.enabled)
            }
        }
        Text {
            anchors.centerIn: parent
            visible: alarmList.count === 0
            text: qsTr("No alarms. Add one with +, or ask the assistant.")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
    }
    Rectangle {
        visible: win.tab === "alarms"
        anchors { right: parent.right; bottom: parent.bottom; margins: 28 }
        width: 64; height: 64; radius: 32
        color: addTap.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent
        Text { anchors.centerIn: parent; text: "+"; color: Theme.bg; font { family: Theme.font; pixelSize: 34 } }
        MouseArea { id: addTap; anchors.fill: parent; onClicked: editor.edit(null) }
    }

    // --- timers ------------------------------------------------------------------------------
    Flickable {
        visible: win.tab === "timers"
        anchors { top: tabs.bottom; topMargin: 20; left: parent.left; right: parent.right; bottom: parent.bottom }
        contentHeight: timerColumn.implicitHeight + 40
        clip: true
        boundsBehavior: Theme.boundsBehavior
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }

        Column {
            id: timerColumn
            anchors { left: parent.left; right: parent.right; margins: 16 }
            spacing: 14

            Repeater {
                model: alarms.timerList
                delegate: Rectangle {
                    id: running
                    required property var modelData
                    width: timerColumn.width
                    height: 96
                    radius: 20
                    color: Theme.panel
                    readonly property real remaining: (modelData.ends - win.now) / 1000
                    Column {
                        anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                        spacing: 2
                        Text {
                            text: win.clockText(running.remaining)
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 38; weight: Font.Light }
                        }
                        Text {
                            text: [running.modelData.label, qsTr("of %1").arg(win.clockText(running.modelData.duration))]
                                  .filter(s => s).join("  ·  ")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                    }
                    Rectangle {
                        anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                        width: 48; height: 48; radius: 24
                        color: cancelTap.pressed ? Theme.line : Theme.bg
                        Text { anchors.centerIn: parent; text: "󰅖"; color: Theme.fg; font { family: Theme.font; pixelSize: 20 } }
                        MouseArea { id: cancelTap; anchors.fill: parent; onClicked: alarms.cancelTimer(running.modelData.id) }
                    }
                    // (how much is left, as a bar along the bottom)
                    Rectangle {
                        anchors { left: parent.left; bottom: parent.bottom; leftMargin: 20; bottomMargin: 10 }
                        height: 3; radius: 1.5
                        width: (parent.width - 40) * Math.max(0, Math.min(1, running.remaining / Math.max(1, running.modelData.duration)))
                        color: Theme.accent
                    }
                }
            }

            Text {
                text: qsTr("Start a timer")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
            Flow {
                width: timerColumn.width
                spacing: 10
                Repeater {
                    model: [1, 3, 5, 10, 15, 30]
                    delegate: Rectangle {
                        id: quick
                        required property int modelData
                        width: (timerColumn.width - 20) / 3; height: 52; radius: 26
                        color: quickTap.pressed ? Theme.line : Theme.panel
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("%1 min").arg(quick.modelData)
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 16 }
                        }
                        MouseArea { id: quickTap; anchors.fill: parent; onClicked: alarms.startTimer(quick.modelData * 60, "") }
                    }
                }
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                Wheel { id: tH; count: 24; value: 0 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: ":"; color: Theme.muted; font { family: Theme.font; pixelSize: 36 } }
                Wheel { id: tM; count: 60; value: 5 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: ":"; color: Theme.muted; font { family: Theme.font; pixelSize: 36 } }
                Wheel { id: tS; count: 60; value: 0 }
            }
            Rectangle {
                readonly property int seconds: tH.value * 3600 + tM.value * 60 + tS.value
                width: timerColumn.width; height: 56; radius: 28
                color: seconds > 0 ? (startTap.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent) : Theme.line
                Text {
                    anchors.centerIn: parent
                    text: "󰐊  " + qsTr("Start")
                    color: Theme.bg
                    font { family: Theme.font; pixelSize: 17; bold: true }
                }
                MouseArea {
                    id: startTap
                    anchors.fill: parent
                    onClicked: if (parent.seconds > 0) alarms.startTimer(parent.seconds, "")
                }
            }
        }
    }

    // --- an alarm, new or to change -----------------------------------------------------
    Rectangle {
        id: editor
        anchors.fill: parent
        visible: false
        color: Theme.bg

        property string alarmId: ""
        property var days: []
        function edit(alarm) {
            alarmId = alarm ? alarm.id : ""
            const parts = (alarm ? alarm.time : Qt.formatTime(new Date(Date.now() + 3600000), "HH:00")).split(":")
            hours.value = parseInt(parts[0], 10)
            minutes.value = parseInt(parts[1], 10)
            days = alarm ? alarm.days.slice() : []
            label.text = alarm ? alarm.label : ""
            visible = true
        }
        function toggleDay(d) {
            const i = days.indexOf(d)
            const next = days.slice()
            if (i >= 0)
                next.splice(i, 1)
            else
                next.push(d)
            days = next.sort()
        }
        function save() {
            const two = n => (n < 10 ? "0" : "") + n
            const time = two(hours.value) + ":" + two(minutes.value)
            if (alarmId === "")
                alarms.addAlarm(time, label.text, days)
            else
                alarms.updateAlarm(alarmId, time, label.text, days)
            visible = false
        }

        MouseArea { anchors.fill: parent }   // (nothing through to the list behind)

        Column {
            anchors { top: parent.top; topMargin: 24; left: parent.left; right: parent.right; margins: 20 }
            spacing: 22

            Item {
                width: parent.width; height: 44
                Text {
                    anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                    text: qsTr("Cancel")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 16 }
                    MouseArea { anchors { fill: parent; margins: -12 } onClicked: editor.visible = false }
                }
                Text {
                    anchors.centerIn: parent
                    text: editor.alarmId === "" ? qsTr("New alarm") : qsTr("Alarm")
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 18; bold: true }
                }
                Text {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                    text: qsTr("Save")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16; bold: true }
                    MouseArea { anchors { fill: parent; margins: -12 } onClicked: editor.save() }
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 10
                Wheel { id: hours; count: 24 }
                Text { anchors.verticalCenter: parent.verticalCenter; text: ":"; color: Theme.muted; font { family: Theme.font; pixelSize: 40 } }
                Wheel { id: minutes; count: 60 }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                Repeater {
                    model: 7
                    delegate: Rectangle {
                        id: day
                        required property int index
                        readonly property bool on: editor.days.indexOf(index + 1) >= 0
                        width: 44; height: 44; radius: 22
                        color: on ? Theme.accent : Theme.panel
                        Text {
                            anchors.centerIn: parent
                            text: win.dayNames[day.index].slice(0, 2)
                            color: day.on ? Theme.bg : Theme.fg
                            font { family: Theme.font; pixelSize: 13; bold: day.on }
                        }
                        MouseArea { anchors.fill: parent; onClicked: editor.toggleDay(day.index + 1) }
                    }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: win.daysText(editor.days)
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }

            Rectangle {
                width: parent.width; height: 52; radius: 14
                color: Theme.panel
                border.color: label.activeFocus ? Theme.accent : Theme.line
                TextInput {
                    id: label
                    anchors { left: parent.left; right: parent.right; leftMargin: 16; rightMargin: 16; verticalCenter: parent.verticalCenter }
                    clip: true
                    color: Theme.fg
                    maximumLength: 80
                    font { family: Theme.font; pixelSize: 16 }
                    Text {
                        visible: label.text === "" && !label.activeFocus
                        text: qsTr("Label (what it's for)")
                        color: Theme.muted
                        font: label.font
                    }
                }
            }

            Text {
                visible: editor.alarmId !== ""
                anchors.horizontalCenter: parent.horizontalCenter
                text: "󰆴  " + qsTr("Delete alarm")
                color: Theme.red
                font { family: Theme.font; pixelSize: 16 }
                MouseArea {
                    anchors { fill: parent; margins: -12 }
                    onClicked: { alarms.removeAlarm(editor.alarmId); editor.visible = false }
                }
            }
        }
    }
}
