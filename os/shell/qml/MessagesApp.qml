pragma ComponentBehavior: Bound

import QtQuick
import QtCore
import Mobarchy.Shell

// Messages (ADR 0031): SMS over ofono (Telephony), kept in messages.json (threads by
// number) so the agent reads and writes them too. A list of threads; a tap opens the
// conversation with a composer. Its own window (mobarchy-shell --messages). A new message
// (the "+") starts a thread: a number, or a contact picked from those matching what is typed.
Window {
    id: win

    visible: true
    title: qsTr("Messages")
    color: Theme.bg

    Telephony {
        id: tel
        onMessagesChanged: win.reload()
    }

    Contacts { id: contacts }
    // a conversation asked for (a notification tapped): at the start, or while open
    AppLink {
        name: "org.mobarchy.Messages"
        service: true
        onShowRequested: (number) => win.show(number)
    }
    function show(number) {
        win.compose = ""
        win.open = number
        win.requestActivate()
    }
    property var threadData: ({})
    property var numbers: []
    property string open: ""          // the thread being read ("" = the list)
    property string compose: ""       // a new thread's number while typing it

    // the newest conversation first
    function lastTime(number) {
        const t = threadData[number]
        return t && t.length ? (t[t.length - 1].time ?? 0) : 0
    }
    function reload() {
        threadData = tel.threads()
        numbers = Object.keys(threadData).sort((a, b) => lastTime(b) - lastTime(a))
        if (open !== "")
            markRead(open)
    }
    // read: when the conversation was last open, per number (unread: a message in since)
    Settings {
        id: readState
        location: StandardPaths.writableLocation(StandardPaths.GenericStateLocation) + "/mobarchy/messages-read.ini"
        property string read: "{}"
    }
    property var readAt: JSON.parse(readState.read)
    function unread(number) {
        const t = threadData[number] ?? []
        const seen = readAt[number] ?? 0
        return t.some(m => m.incoming && (m.time ?? 0) > seen)
    }
    function markRead(number) {
        const r = Object.assign({}, readAt)
        r[number] = Math.floor(Date.now() / 1000)
        readAt = r
        readState.read = JSON.stringify(r)
    }
    onOpenChanged: if (open !== "") markRead(open)
    // mobarchy-shell --messages [number]
    Component.onCompleted: {
        reload()
        const args = Qt.application.arguments
        const i = args.indexOf("--messages")
        if (i >= 0 && i + 1 < args.length)
            Qt.callLater(() => win.show(args[i + 1]))
    }

    // --- thread list ---
    Item {
        anchors.fill: parent
        visible: win.open === "" && win.compose === ""

        Text {
            id: title
            anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 22 }
            text: qsTr("Messages")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 26; bold: true }
        }
        Rectangle {
            anchors { right: parent.right; rightMargin: 18; verticalCenter: title.verticalCenter }
            width: 40; height: 40; radius: 20
            color: newTap.pressed ? Theme.line : Theme.panel
            Text { anchors.centerIn: parent; text: "+"; color: Theme.fg; font.pixelSize: 26 }
            TapHandler { id: newTap; onTapped: win.compose = " " }   // a space marks "composing"
        }

        ListView {
            anchors { left: parent.left; right: parent.right; top: title.bottom; topMargin: 14; bottom: parent.bottom }
            clip: true
            boundsBehavior: Theme.boundsBehavior
            model: win.numbers
            delegate: Rectangle {
                id: row
                required property string modelData
                width: ListView.view.width
                height: 68
                color: rowTap.pressed ? Theme.panel : "transparent"
                readonly property var last: {
                    const t = win.threadData[modelData]
                    return t && t.length ? t[t.length - 1] : null
                }
                readonly property bool unread: win.unread(modelData)
                // unread: a dot, and the name and the last message bright
                Rectangle {
                    visible: row.unread
                    anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                    width: 8; height: 8; radius: 4
                    color: Theme.accent
                }
                Column {
                    anchors { left: parent.left; leftMargin: 24; right: when.left; rightMargin: 12; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: contacts.nameFor(row.modelData) || row.modelData
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 16; bold: row.unread }
                    }
                    Text {
                        width: parent.width
                        // (one line: a long message's lines run into the next row otherwise)
                        text: (row.last && row.last.incoming ? "" : "→ ") + (row.last ? row.last.text.replace(/\s+/g, " ") : "")
                        maximumLineCount: 1
                        color: row.unread ? Theme.fg : Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                        elide: Text.ElideRight
                    }
                }
                Text {
                    id: when
                    anchors { right: parent.right; rightMargin: 20; top: parent.top; topMargin: 14 }
                    text: {
                        if (!row.last || !row.last.time)
                            return ""
                        const d = new Date(row.last.time * 1000)
                        return d.toDateString() === new Date().toDateString() ? Qt.formatTime(d, "HH:mm")
                             : d.toLocaleDateString(Qt.locale(), "d MMM")
                    }
                    color: row.unread ? Theme.accent : Theme.muted
                    font { family: Theme.font; pixelSize: 12 }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line; opacity: 0.4 }
                TapHandler { id: rowTap; onTapped: win.open = row.modelData }
            }
        }
    }

    // --- a conversation (or a new one) ---
    Item {
        anchors.fill: parent
        visible: win.open !== "" || win.compose !== ""
        readonly property string number: win.open !== "" ? win.open : win.compose.trim()

        // header: back + number (or a field for a new number)
        Item {
            id: head
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: "‹"; color: Theme.accent; font { family: Theme.font; pixelSize: 34 }
                TapHandler { onTapped: { win.open = ""; win.compose = "" } }
            }
            Text {
                visible: win.open !== ""
                anchors.centerIn: parent
                text: contacts.nameFor(win.open) || win.open
                color: Theme.fg; font { family: Theme.font; pixelSize: 17 }
            }
            TextInput {
                id: toField
                visible: win.compose !== ""
                anchors { left: parent.left; leftMargin: 60; right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: ""
                onTextChanged: win.compose = " " + text
                color: Theme.fg; font { family: Theme.font; pixelSize: 17 }
                Text { visible: parent.text === ""; text: qsTr("Name or number"); color: Theme.muted; font: parent.font }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line; opacity: 0.4 }
        }

        // a new message: the contacts that match what is typed, to pick one
        ContactList {
            visible: win.compose !== ""
            anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: composer.top }
            z: 1
            book: contacts
            showSearch: false
            query: toField.text
            actions: ["message", "copy"]
            onMessage: (number) => win.show(number)
        }
        ListView {
            id: thread
            anchors { left: parent.left; right: parent.right; top: head.bottom; bottom: composer.top; margins: 12 }
            clip: true
            spacing: 8
            boundsBehavior: Theme.boundsBehavior
            model: win.open !== "" ? (win.threadData[win.open] ?? []) : []
            delegate: Row {
                required property var modelData
                width: thread.width
                height: childrenRect.height
                layoutDirection: modelData.incoming ? Qt.LeftToRight : Qt.RightToLeft
                Rectangle {
                    width: Math.min(bubble.implicitWidth + 24, thread.width * 0.8)
                    height: bubble.implicitHeight + 18
                    radius: 16
                    color: modelData.incoming ? Theme.panel : Theme.accent
                    Text {
                        id: bubble
                        anchors { left: parent.left; right: parent.right; margins: 12; verticalCenter: parent.verticalCenter }
                        text: modelData.text
                        wrapMode: Text.Wrap
                        color: modelData.incoming ? Theme.fg : Theme.bg
                        font { family: Theme.font; pixelSize: 15 }
                    }
                }
            }
            onCountChanged: positionViewAtEnd()
        }

        // composer
        Rectangle {
            id: composer
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 60
            color: Theme.bgDark
            TextInput {
                id: body
                anchors { left: parent.left; leftMargin: 18; right: sendBtn.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                color: Theme.fg; font { family: Theme.font; pixelSize: 16 }
                wrapMode: TextInput.Wrap
                Text { visible: parent.text === ""; text: qsTr("Message"); color: Theme.muted; font: parent.font }
            }
            Rectangle {
                id: sendBtn
                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                width: 56; height: 40; radius: 20
                // (a new one: to what is typed if it is a number; a name is for picking a contact)
                readonly property string to: win.open !== "" ? win.open
                    : /^\+?[0-9 -]{3,}$/.test(win.compose.trim()) ? win.compose.trim().replace(/[ -]/g, "") : ""
                enabled: body.text !== "" && to !== ""
                color: enabled ? Theme.accent : Theme.line
                Text { anchors.centerIn: parent; text: qsTr("Send"); color: Theme.bg; font { family: Theme.font; pixelSize: 13; bold: true } }
                TapHandler {
                    onTapped: {
                        if (!sendBtn.enabled) return
                        tel.sendMessage(sendBtn.to, body.text)
                        if (win.compose !== "") { win.open = sendBtn.to; win.compose = "" }
                        body.text = ""
                        win.reload()
                    }
                }
            }
        }
    }
}
