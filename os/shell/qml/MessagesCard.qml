pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The latest from people: text messages and missed calls, newest first, with names from the
// contacts. Tapping one opens Messages or Phone.
BoardCard {
    id: card

    required property Telephony telephony
    required property AppModel apps

    property var all: []        // [{who, text, time, kind: "sms" | "missed"}], newest first
    // as many as there is room for: two, or five in a large card
    readonly property var items: all.slice(0, rows >= 4 ? 5 : 2)

    wanted: items.length > 0
    title: qsTr("Messages")
    icon: "󰍡"

    Contacts { id: contacts }

    function refresh() {
        const out = []
        const threads = telephony.threads()
        for (const number in threads) {
            const thread = threads[number]
            if (thread.length === 0)
                continue
            const last = thread[thread.length - 1]
            out.push({ who: contacts.nameFor(number) || number, kind: "sms", time: last.time ?? 0,
                       text: (last.incoming ? "" : qsTr("You: ")) + last.text })
        }
        for (const c of telephony.recentCalls().slice(0, 20)) {
            if (!c.outgoing && !c.answered)
                out.push({ who: contacts.nameFor(c.number) || c.number || qsTr("Unknown"), kind: "missed",
                           time: c.time, text: qsTr("Missed call") })
        }
        out.sort((a, b) => b.time - a.time)
        all = out.slice(0, 5)
    }
    function ago(epoch) {
        const at = new Date(epoch * 1000)
        return at.toDateString() === new Date().toDateString()
               ? at.toLocaleTimeString(Qt.locale(), "HH:mm") : at.toLocaleDateString(Qt.locale(), "d. MMM")
    }

    Component.onCompleted: refresh()
    Connections {
        target: card.telephony
        function onMessagesChanged() { card.refresh() }
        function onCallsChanged() { card.refresh() }
        function onMessageReceived() { card.refresh() }
    }
    Connections {
        target: contacts
        function onChanged() { card.refresh() }
    }

    Column {
        anchors.fill: parent
        spacing: 8
        Repeater {
            model: card.items
            delegate: Item {
                id: row
                required property var modelData
                width: parent.width
                height: 40
                MouseArea {
                    anchors.fill: parent
                    onClicked: card.apps.launch(row.modelData.kind === "sms" ? "org.mobarchy.Messages" : "org.mobarchy.Phone")
                }
                Text {
                    id: who
                    anchors { left: parent.left; top: parent.top }
                    width: Math.min(implicitWidth, parent.width - when.width - 12)
                    elide: Text.ElideRight
                    text: row.modelData.who
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 14; bold: true }
                }
                Text {
                    id: when
                    anchors { right: parent.right; top: parent.top }
                    text: card.ago(row.modelData.time)
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 11 }
                }
                Text {
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                    elide: Text.ElideRight
                    text: row.modelData.text
                    color: row.modelData.kind === "missed" ? Theme.red : Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
            }
        }
    }
}
