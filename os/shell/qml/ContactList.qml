pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The owner's contacts as a list (Contacts, Phone, Messages): a name, then each number with
// what can be done with it: call, message, copy. A tap on a number does the first of them.
Item {
    id: list

    required property Contacts book
    // what the numbers offer, in order: "call", "message", "copy"
    property var actions: ["call", "message", "copy"]
    property bool showSearch: true
    property string query: search.text      // (or the caller's: a "to" field)
    signal call(string number)
    signal message(string number)

    Clipboard { id: clipboard }
    function act(what, number) {
        if (what === "call")
            call(number)
        else if (what === "message")
            message(number)
        else {
            clipboard.setText(number)
            copied.show(number)
        }
    }

    Rectangle {
        id: searchBox
        visible: list.showSearch
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        height: visible ? 44 : 0
        radius: 22
        color: Theme.panel
        TextInput {
            id: search
            anchors { fill: parent; leftMargin: 18; rightMargin: 18 }
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.fg
            font { family: Theme.font; pixelSize: 16 }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.text === ""
                text: qsTr("Search contacts")
                color: Theme.muted
                font: parent.font
            }
        }
    }
    Text {
        anchors.centerIn: parent
        width: parent.width - 60
        visible: list.book.all.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        text: qsTr("No contacts yet. Sign in to an account (Accounts), or ask Frigg to add someone.")
        color: Theme.muted
        font { family: Theme.font; pixelSize: 15 }
    }
    ListView {
        anchors { left: parent.left; right: parent.right; top: searchBox.bottom; topMargin: 8; bottom: parent.bottom }
        clip: true
        boundsBehavior: Theme.boundsBehavior
        // (all named: the list follows the contacts as they change)
        model: list.book.all ? list.book.filter(list.query) : []
        delegate: Column {
            id: person
            required property var modelData
            width: ListView.view.width
            topPadding: 10; bottomPadding: 10
            Text {
                x: 20
                width: person.width - 40
                elide: Text.ElideRight
                text: person.modelData.name + (person.modelData.relations.length
                      ? "  · " + person.modelData.relations.join(", ") : "")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 17 }
            }
            Repeater {
                model: person.modelData.phones
                delegate: Rectangle {
                    id: num
                    required property var modelData
                    width: person.width; height: 44
                    color: numTap.pressed ? Theme.panel : "transparent"
                    TapHandler { id: numTap; onTapped: list.act(list.actions[0], num.modelData.number) }
                    Text {
                        x: 20; anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - buttons.width - 40
                        elide: Text.ElideRight
                        text: num.modelData.number + (num.modelData.type ? "   " + num.modelData.type : "")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    Row {
                        id: buttons
                        anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                        spacing: 4
                        Repeater {
                            model: list.actions
                            delegate: Item {
                                id: button
                                required property string modelData
                                width: 44; height: 44
                                Text {
                                    anchors.centerIn: parent
                                    text: ({ call: "󰏲", message: "󰍦", copy: "󰆏" })[button.modelData]
                                    color: button.modelData === "call" ? Theme.green
                                         : button.modelData === "message" ? Theme.accent : Theme.muted
                                    font { family: Theme.font; pixelSize: 21 }
                                }
                                TapHandler { onTapped: list.act(button.modelData, num.modelData.number) }
                            }
                        }
                    }
                }
            }
        }
    }

    // "Copied", for a moment
    Rectangle {
        id: copied
        function show(number) {
            copiedText.text = qsTr("Copied %1").arg(number)
            opacity = 1
            fade.restart()
        }
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 24 }
        width: copiedText.implicitWidth + 32; height: 40; radius: 20
        color: Theme.fg
        opacity: 0
        Text { id: copiedText; anchors.centerIn: parent; color: Theme.bg; font { family: Theme.font; pixelSize: 14 } }
        SequentialAnimation {
            id: fade
            PauseAnimation { duration: 1400 }
            NumberAnimation { target: copied; property: "opacity"; to: 0; duration: 300 }
        }
    }
}
