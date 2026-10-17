pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Messages (ADR 0031): SMS over ofono (Telephony), kept in messages.json (threads by
// number) so the agent reads and writes them too. A list of threads; a tap opens the
// conversation with a composer. Its own window (mobarchy-shell --messages). A new message
// (the "+" or a tap) starts a thread by number — no contacts yet.
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
    property var threadData: ({})
    property var numbers: []
    property string open: ""          // the thread being read ("" = the list)
    property string compose: ""       // a new thread's number while typing it

    function reload() {
        threadData = tel.threads()
        numbers = Object.keys(threadData).sort()
    }
    Component.onCompleted: reload()

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
                Column {
                    anchors { left: parent.left; leftMargin: 20; right: parent.right; rightMargin: 20; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text { text: contacts.nameFor(row.modelData) || row.modelData; color: Theme.fg; font { family: Theme.font; pixelSize: 16 } }
                    Text {
                        width: parent.width
                        text: (row.last && row.last.incoming ? "" : "→ ") + (row.last ? row.last.text : "")
                        color: Theme.muted; font { family: Theme.font; pixelSize: 13 }
                        elide: Text.ElideRight
                    }
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
                inputMethodHints: Qt.ImhDialableCharactersOnly
                Text { visible: parent.text === ""; text: qsTr("Phone number"); color: Theme.muted; font: parent.font }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line; opacity: 0.4 }
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
                readonly property string to: win.open !== "" ? win.open : win.compose.trim()
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
