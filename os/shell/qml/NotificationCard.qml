import QtQuick
import Mobarchy.Shell

// A notification as a card (quick settings' list, the lock screen): the app's icon, its name
// and when, what it says, its actions; a tap opens it, ✕ dismisses it. Fed by a delegate of
// Notifications.
Rectangle {
    id: note

    required property int notificationId
    required property string app
    required property string icon
    required property string summary
    required property string body
    required property var actions
    required property int urgency
    required property date time
    property bool showActions: true
    property color ground: Theme.panel
    signal activated()
    signal dismissed()
    signal action(string key)

    height: noteColumn.implicitHeight + 24
    radius: 14
    color: noteTap.pressed ? Theme.line : ground
    border.color: urgency >= 2 ? Theme.red : "transparent"

    MouseArea { id: noteTap; anchors.fill: parent; onClicked: note.activated() }
    Image {
        id: noteIcon
        anchors { left: parent.left; leftMargin: 14; top: parent.top; topMargin: 14 }
        width: 28; height: 28
        sourceSize: Qt.size(56, 56)
        asynchronous: true
        source: note.icon ? "image://icon/" + note.icon : ""
        visible: status === Image.Ready
    }
    Column {
        id: noteColumn
        anchors { left: noteIcon.visible ? noteIcon.right : parent.left; leftMargin: 14
                  right: closeNote.left; top: parent.top; topMargin: 12 }
        spacing: 3
        Text {
            width: parent.width
            text: note.app + "  ·  " + Qt.formatTime(note.time, "HH:mm")
            elide: Text.ElideRight
            color: Theme.muted
            font { family: Theme.font; pixelSize: 11 }
        }
        Text {
            width: parent.width
            text: note.summary
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            color: Theme.fg
            font { family: Theme.font; pixelSize: 13; bold: true }
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: note.body
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            color: Theme.fg
            font { family: Theme.font; pixelSize: 12 }
        }
        Row {
            visible: note.showActions && note.actions.length > 0
            spacing: 8
            topPadding: 4
            Repeater {
                model: note.actions
                delegate: Rectangle {
                    id: actionButton
                    required property var modelData
                    width: actionText.implicitWidth + 24; height: 30; radius: 15
                    color: "transparent"
                    border.color: Theme.accent
                    Text {
                        id: actionText
                        anchors.centerIn: parent
                        text: actionButton.modelData.label
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 11 }
                    }
                    MouseArea {
                        anchors { fill: parent; margins: -4 }
                        onClicked: note.action(actionButton.modelData.key)
                    }
                }
            }
        }
    }
    MouseArea {
        id: closeNote
        anchors { right: parent.right; top: parent.top }
        width: 44; height: 44
        onClicked: note.dismissed()
        Text {
            anchors.centerIn: parent
            text: "󰅖"
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
    }
}
