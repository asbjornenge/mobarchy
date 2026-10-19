pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Contacts (ADR 0031, 0032): the owner's contacts, searched; each number can be called,
// messaged or copied. Its own window (mobarchy-shell --contacts). The contacts come from the
// owner's own file and the accounts' (read here; Frigg and the accounts' sync write them).
Window {
    id: win

    visible: true
    title: qsTr("Contacts")
    color: Theme.bg

    Contacts { id: contacts }
    Telephony { id: tel }

    Text {
        id: heading
        anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 22 }
        text: qsTr("Contacts")
        color: Theme.fg
        font { family: Theme.font; pixelSize: 26; bold: true }
    }
    ContactList {
        anchors { left: parent.left; right: parent.right; top: heading.bottom; topMargin: 6; bottom: parent.bottom }
        book: contacts
        // (the call shows in the Phone app: the shell brings it up for a call going out)
        onCall: (number) => tel.dial(number)
        onMessage: (number) => MessagesThread.open(number)
    }
}
