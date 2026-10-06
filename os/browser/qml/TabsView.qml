import QtQuick

// The tabs as cards, two to a row: tap one to go to it, its cross closes it, and the
// button at the bottom opens a new one.
Rectangle {
    id: view
    property var tabs: []            // the tabs' views
    property int current: 0
    signal pick(int index)
    signal close(int index)
    signal newTab()
    signal done()

    color: Theme.bg
    visible: false
    MouseArea { anchors.fill: parent }  // (the page and its bar below take no taps)

    GridView {
        id: grid
        anchors { fill: parent; margins: 12; bottomMargin: 88 }
        clip: true
        currentIndex: -1
        cellWidth: width / (width > 700 ? 3 : 2)
        cellHeight: cellWidth * 1.25
        model: view.tabs.length
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        delegate: Item {
            id: cell
            required property int index
            readonly property var tab: view.tabs[index]
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                anchors { fill: parent; margins: 6 }
                radius: 16
                color: Theme.panel
                border { color: cell.index === view.current ? Theme.accent : Theme.line
                         width: cell.index === view.current ? 3 : 1 }
                Column {
                    anchors { fill: parent; margins: 14; topMargin: 16 }
                    spacing: 8
                    Image {
                        width: 28; height: 28
                        source: cell.tab ? cell.tab.icon : ""
                        sourceSize: Qt.size(56, 56)
                    }
                    Text {
                        width: parent.width - 20
                        text: cell.tab ? (cell.tab.title || qsTr("New tab")) : ""
                        color: Theme.fg
                        wrapMode: Text.Wrap; maximumLineCount: 4; elide: Text.ElideRight
                        font { family: Theme.font; pixelSize: 15; bold: true }
                    }
                    Text {
                        width: parent.width
                        text: cell.tab && cell.tab.url.toString() ? new URL(cell.tab.url.toString()).hostname : ""
                        color: Theme.muted; elide: Text.ElideRight
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: view.pick(cell.index) }
                Text {
                    anchors { right: parent.right; top: parent.top; margins: 10 }
                    text: "󰅖"; color: Theme.muted
                    font { family: Theme.font; pixelSize: 20 }
                    MouseArea { anchors { fill: parent; margins: -14 } onClicked: view.close(cell.index) }
                }
            }
        }
    }

    Row {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 16 }
        spacing: 16
        Rectangle {
            width: 160; height: 52; radius: 26
            color: newTap.pressed ? Theme.line : Theme.accent
            Text {
                anchors.centerIn: parent
                text: "󰐕  " + qsTr("New tab"); color: Theme.bg
                font { family: Theme.font; pixelSize: 15; bold: true }
            }
            MouseArea { id: newTap; anchors.fill: parent; onClicked: view.newTab() }
        }
        Rectangle {
            width: 120; height: 52; radius: 26
            color: doneTap.pressed ? Theme.line : Theme.panel
            Text {
                anchors.centerIn: parent
                text: qsTr("Done"); color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
            }
            MouseArea { id: doneTap; anchors.fill: parent; onClicked: view.done() }
        }
    }
}
