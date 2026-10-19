pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// An event, in a sheet over the calendar: a read-only one as it is, a new one or one that can
// be changed (the phone's, or an account's the sign-in lets the phone write to) as a form: the title, all day or a time, the day, how long, whether it
// repeats, where, notes.
Item {
    id: sheet

    property var event: null        // {} with isNew for a new one
    required property CalendarData store
    signal done()

    readonly property bool isNew: event !== null && event.isNew === true
    readonly property bool editable: event !== null && (isNew || event.editable === true)

    // the form's state, from the event when it opens
    property string title
    property bool allDay
    property date day: new Date()
    property int lengthMinutes: 60
    property string repeat: ""
    property string place
    property string notes
    onEventChanged: {
        if (!event)
            return
        const s = new Date(event.start), e = new Date(event.end)
        title = event.title ?? ""
        allDay = event.all_day ?? false
        day = s
        hours.value = s.getHours()
        minutes.value = Math.round(s.getMinutes() / 5) % 12
        lengthMinutes = Math.max(15, Math.round((e - s) / 60000))
        repeat = ""
        place = event.location ?? ""
        notes = event.notes ?? ""
        titleInput.text = title
        placeInput.text = place
        notesInput.text = notes
        if (isNew)
            titleInput.forceActiveFocus()
    }

    function stamp(d, withTime) {
        return Qt.formatDate(d, "yyyy-MM-dd") + (withTime ? "T" + Qt.formatTime(d, "HH:mm") : "")
    }
    function save() {
        const t = titleInput.text.trim()
        if (!t)
            return
        let start, end
        if (allDay) {
            start = stamp(day, false)
            end = stamp(new Date(day.getFullYear(), day.getMonth(), day.getDate() + 1), false)
        } else {
            const s = new Date(day.getFullYear(), day.getMonth(), day.getDate(), hours.value, minutes.value * 5)
            start = stamp(s, true)
            end = stamp(new Date(s.getTime() + lengthMinutes * 60000), true)
        }
        if (isNew)
            store.add(t, start, end, placeInput.text, notesInput.text, repeat)
        else
            store.update(event.uid, t, start, end, placeInput.text, notesInput.text)
        done()
    }

    // the dim behind; a tap there closes
    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Theme.bgDark, 0.7)
        MouseArea { anchors.fill: parent; onClicked: sheet.done() }
    }

    Rectangle {
        id: card
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Math.min(parent.height - 40, content.implicitHeight + 40)
        radius: 26
        color: Theme.bg
        border.color: Theme.line
        MouseArea { anchors.fill: parent }     // (taps on the card stay on it)

        Flickable {
            anchors { fill: parent; margins: 20; bottomMargin: 0 }
            contentHeight: content.implicitHeight + 20
            clip: true
            boundsBehavior: Theme.boundsBehavior

            Column {
                id: content
                width: parent.width
                spacing: 14

                // --- as it is (an account's) ---
                Column {
                    visible: !sheet.editable
                    width: parent.width
                    spacing: 8
                    Row {
                        spacing: 10
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 12; height: 12; radius: 4
                            color: sheet.event ? sheet.event.color : Theme.accent
                        }
                        Text {
                            text: sheet.event ? sheet.event.calendar_name : ""
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: sheet.event ? sheet.event.title ?? "" : ""
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 22; bold: true }
                    }
                    Text {
                        text: !sheet.event ? "" : new Date(sheet.event.start).toLocaleDateString(Qt.locale(), "dddd d. MMMM")
                              + (sheet.event.all_day ? "" : "  ·  " + Qt.formatTime(new Date(sheet.event.start), "HH:mm")
                                 + " – " + Qt.formatTime(new Date(sheet.event.end), "HH:mm"))
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 15 }
                    }
                    Text {
                        visible: text !== ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: sheet.event && sheet.event.location ? "󰍎  " + sheet.event.location : ""
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    Text {
                        visible: text !== ""
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: sheet.event ? sheet.event.notes ?? "" : ""
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    Text {
                        topPadding: 6
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Read only here. If it is your own calendar, sign in to the account again in Accounts to change it.")
                        color: Qt.alpha(Theme.muted, 0.8)
                        font { family: Theme.font; pixelSize: 12; italic: true }
                    }
                }

                // --- the form ---
                component Field: Rectangle {
                    id: field
                    property alias input: inner
                    property string hint
                    width: content.width
                    height: 46
                    radius: 12
                    color: Theme.panel
                    TextInput {
                        id: inner
                        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                        verticalAlignment: TextInput.AlignVCenter
                        clip: true
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 15 }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: !inner.text && !inner.activeFocus
                            text: field.hint
                            color: Theme.muted
                            font: inner.font
                        }
                    }
                }
                component Chip: Rectangle {
                    id: chip
                    property string label
                    property bool on: false
                    signal clicked()
                    width: chipText.implicitWidth + 24
                    height: 34
                    radius: 17
                    color: on ? Theme.accent : Theme.panel
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.label
                        color: chip.on ? Theme.bg : Theme.fg
                        font { family: Theme.font; pixelSize: 13; bold: chip.on }
                    }
                    MouseArea { anchors.fill: parent; anchors.margins: -3; onClicked: chip.clicked() }
                }

                Field {
                    id: titleField
                    visible: sheet.editable
                    hint: qsTr("Title")
                }

                // the day: back and forth
                Item {
                    visible: sheet.editable
                    width: parent.width
                    height: 40
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: "‹"
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 30 }
                        MouseArea { anchors { fill: parent; margins: -12 }
                                    onClicked: sheet.day = new Date(sheet.day.getFullYear(), sheet.day.getMonth(), sheet.day.getDate() - 1) }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: sheet.day.toLocaleDateString(Qt.locale(), "dddd d. MMMM")
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 16; bold: true; capitalization: Font.Capitalize }
                    }
                    Text {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: "›"
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 30 }
                        MouseArea { anchors { fill: parent; margins: -12 }
                                    onClicked: sheet.day = new Date(sheet.day.getFullYear(), sheet.day.getMonth(), sheet.day.getDate() + 1) }
                    }
                }

                Row {
                    visible: sheet.editable
                    spacing: 12
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("All day")
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 15 }
                    }
                    Switch {
                        anchors.verticalCenter: parent.verticalCenter
                        checked: sheet.allDay
                        onToggled: sheet.allDay = !sheet.allDay
                    }
                }

                // the time, and how long
                Row {
                    visible: sheet.editable && !sheet.allDay
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 6
                    height: 156
                    Wheel { id: hours; count: 24; height: 156 }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: ":"; color: Theme.muted; font { family: Theme.font; pixelSize: 32 } }
                    Wheel { id: minutes; count: 12; step: 5; height: 156 }
                }
                Flow {
                    visible: sheet.editable && !sheet.allDay
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: [15, 30, 45, 60, 90, 120, 180]
                        delegate: Chip {
                            required property int modelData
                            label: modelData < 60 ? modelData + " min" : (modelData / 60) + " h"
                            on: sheet.lengthMinutes === modelData
                            onClicked: sheet.lengthMinutes = modelData
                        }
                    }
                }
                // repeating (new ones)
                Flow {
                    visible: sheet.isNew
                    width: parent.width
                    spacing: 8
                    Repeater {
                        model: [{ id: "", label: qsTr("Once") }, { id: "daily", label: qsTr("Daily") },
                                { id: "weekly", label: qsTr("Weekly") }, { id: "monthly", label: qsTr("Monthly") },
                                { id: "yearly", label: qsTr("Yearly") }]
                        delegate: Chip {
                            required property var modelData
                            label: modelData.label
                            on: sheet.repeat === modelData.id
                            onClicked: sheet.repeat = modelData.id
                        }
                    }
                }
                Field { id: placeField; visible: sheet.editable; hint: qsTr("Where") }
                Field { id: notesField; visible: sheet.editable; hint: qsTr("Notes") }

                // save, delete
                Row {
                    visible: sheet.editable
                    spacing: 10
                    Rectangle {
                        width: 140; height: 46; radius: 23
                        color: saveTap.pressed ? Qt.darker(Theme.accent, 1.2) : Theme.accent
                        opacity: titleField.input.text.trim() ? 1 : 0.4
                        Text {
                            anchors.centerIn: parent
                            text: sheet.isNew ? qsTr("Add") : qsTr("Save")
                            color: Theme.bg
                            font { family: Theme.font; pixelSize: 15; bold: true }
                        }
                        MouseArea { id: saveTap; anchors.fill: parent; onClicked: sheet.save() }
                    }
                    Rectangle {
                        visible: !sheet.isNew
                        width: 120; height: 46; radius: 23
                        color: "transparent"
                        border { color: Theme.red; width: 1 }
                        Text {
                            anchors.centerIn: parent
                            text: qsTr("Delete")
                            color: Theme.red
                            font { family: Theme.font; pixelSize: 15 }
                        }
                        MouseArea { anchors.fill: parent; onClicked: { sheet.store.remove(sheet.event.uid); sheet.done() } }
                    }
                }
            }
        }
    }
    // (the form's inputs, by name)
    readonly property alias titleInput: titleField.input
    readonly property alias placeInput: placeField.input
    readonly property alias notesInput: notesField.input
}
