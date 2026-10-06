import QtQuick

// Editing the address: the whole window, the field at the top (the keyboard takes the
// bottom), and what was visited before that matches, as big rows.
Rectangle {
    id: sheet
    property var history: []         // [{url, title}], newest first
    property string current          // the page's URL, to start from
    signal go(string text)
    signal cancel()

    color: Theme.bg
    visible: false
    MouseArea { anchors.fill: parent }  // (the page and its bar below take no taps)

    function open() {
        field.text = current
        visible = true
        field.forceActiveFocus()
        field.selectAll()
    }
    function close() {
        field.focus = false
        Qt.inputMethod.hide()
        visible = false
    }

    // What the text could be: a search for it first, then the visits that match.
    readonly property var matches: {
        const q = field.text.trim()
        const lq = q.toLowerCase()
        if (!q || lq === current.toLowerCase())
            return history.slice(0, 8)
        const found = history.filter(h => h.url.toLowerCase().includes(lq)
                                          || (h.title || "").toLowerCase().includes(lq)).slice(0, 7)
        return [{url: q, title: qsTr("Search for “%1”").arg(q), search: true}].concat(found)
    }

    Rectangle {
        id: top
        width: parent.width; height: 72
        color: Theme.bgDark
        Rectangle {
            anchors { left: parent.left; right: cancelText.left; margins: 12; verticalCenter: parent.verticalCenter }
            height: 50; radius: 25
            color: Theme.panel
            border { color: Theme.accent; width: 2 }
            TextInput {
                id: field
                anchors { fill: parent; leftMargin: 20; rightMargin: 20 }
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.fg
                selectionColor: Theme.accent
                selectedTextColor: Theme.bg
                font { family: Theme.font; pixelSize: 17 }
                clip: true
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onAccepted: if (text.trim()) sheet.go(text.trim())
            }
            Text {
                visible: !field.text
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: qsTr("Search or address")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 17 }
            }
        }
        Text {
            id: cancelText
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            text: qsTr("Cancel")
            color: Theme.accent
            font { family: Theme.font; pixelSize: 15 }
            MouseArea { anchors { fill: parent; margins: -12 } onClicked: sheet.cancel() }
        }
    }

    ListView {
        anchors { top: top.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }
        clip: true
        currentIndex: -1
        model: sheet.matches
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        delegate: Rectangle {
            id: row
            required property var modelData
            width: ListView.view.width; height: 64
            color: rowTap.pressed ? Theme.panel : "transparent"
            Text {
                id: rowIcon
                anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                text: row.modelData.search ? "󰍉" : "󰋚"; color: Theme.muted
                font { family: Theme.font; pixelSize: 18 }
            }
            Column {
                anchors { left: rowIcon.right; leftMargin: 16; right: parent.right; rightMargin: 16
                          verticalCenter: parent.verticalCenter }
                Text {
                    width: parent.width
                    text: row.modelData.title || row.modelData.url
                    color: Theme.fg; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 15 }
                }
                Text {
                    visible: !row.modelData.search
                    width: parent.width
                    text: row.modelData.url
                    color: Theme.muted; elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12 }
                }
            }
            MouseArea { id: rowTap; anchors.fill: parent; onClicked: sheet.go(row.modelData.url) }
        }
    }
}
