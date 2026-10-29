pragma ComponentBehavior: Bound

import QtQuick

// The tabs as cards, two to a row: each with a picture of its page as it was last shown (a
// tab not shown since the start has its icon and title only). Tap one to go to it, its
// cross closes it, and the button at the bottom opens a new one.
//
// The page shrinks into its card as the list opens (open(true)), and the card's picture
// grows to fill the screen as one is chosen: the page itself takes over from it then.
Rectangle {
    id: view
    property var tabs: []            // the tabs' views; each one's `shot`: its picture
    property int current: 0
    signal pick(int index)
    signal close(int index)
    signal newTab()
    signal done()

    color: Theme.bg
    visible: false
    MouseArea { anchors.fill: parent }  // (the page and its bar below take no taps)

    readonly property int zoomDuration: 240

    // Shown; from the page in front, shrinking into its card (its picture just taken)
    function open(fromPage) {
        visible = true
        grid.positionViewAtIndex(current, GridView.Contain)
        const tab = tabs[current]
        if (!fromPage || !tab || !tab.shot)
            return
        zoom.source = tab.shot.url
        zoom.place(Qt.rect(0, 0, view.width, view.height))
        zoom.visible = true
        grid.opacity = 0
        // (where its card is, once the grid has laid it out)
        Qt.callLater(() => {
            const r = view.thumbRect(view.current)
            if (!r) {
                zoom.visible = false
                grid.opacity = 1
                return
            }
            zoom.animateTo(r, () => zoom.visible = false)
            gridIn.restart()
        })
    }
    // A card chosen: its picture grows to the screen, then the page is shown
    function choose(index) {
        const tab = tabs[index]
        const r = thumbRect(index)
        if (!tab || !tab.shot || !r) {
            pick(index)
            return
        }
        zoom.source = tab.shot.url
        zoom.place(r)
        zoom.visible = true
        gridOut.restart()
        zoom.animateTo(Qt.rect(0, 0, view.width, view.height), () => {
            view.pick(index)
            zoom.visible = false
            grid.opacity = 1
        })
    }
    // a card's picture, in the view's coordinates
    function thumbRect(index) {
        const cell = grid.itemAtIndex(index)
        if (!cell || !cell.thumb.visible)
            return null
        const p = cell.thumb.mapToItem(view, 0, 0)
        return Qt.rect(p.x, p.y, cell.thumb.width, cell.thumb.height)
    }

    GridView {
        id: grid
        anchors { fill: parent; margins: 12; bottomMargin: 88 }
        clip: true
        currentIndex: -1
        cellWidth: width / (width > 700 ? 3 : 2)
        // (a picture of the page: about a phone screen's shape)
        cellHeight: cellWidth * 1.6
        model: view.tabs.length
        maximumFlickVelocity: Theme.flickVelocity
        flickDeceleration: Theme.flickDeceleration
        delegate: Item {
            id: cell
            required property int index
            readonly property var tab: view.tabs[index]
            readonly property alias thumb: thumb
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                id: frame
                anchors { fill: parent; margins: 6 }
                radius: 16
                color: Theme.panel
                border { color: cell.index === view.current ? Theme.accent : Theme.line
                         width: cell.index === view.current ? 3 : 1 }
                clip: true
                Row {
                    id: head
                    anchors { left: parent.left; right: parent.right; top: parent.top
                              leftMargin: 12; rightMargin: 40; topMargin: 12 }
                    spacing: 8
                    Image {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18
                        source: cell.tab ? cell.tab.icon : ""
                        sourceSize: Qt.size(36, 36)
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: head.width - 26
                        text: cell.tab ? (cell.tab.title || qsTr("New tab")) : ""
                        color: Theme.fg
                        elide: Text.ElideRight
                        font { family: Theme.font; pixelSize: 13; bold: true }
                    }
                }
                // the page as last shown
                Image {
                    id: thumb
                    visible: !!(cell.tab && cell.tab.shot)
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                              top: head.bottom; topMargin: 10; margins: 3 }
                    source: visible ? cell.tab.shot.url : ""
                    fillMode: Image.PreserveAspectCrop
                    verticalAlignment: Image.AlignTop
                    smooth: true
                }
                // not shown yet since the start: the title, larger, and the site
                Column {
                    visible: !thumb.visible
                    anchors { left: parent.left; right: parent.right; top: head.bottom; margins: 12 }
                    spacing: 8
                    Text {
                        width: parent.width
                        text: cell.tab ? (cell.tab.title || qsTr("New tab")) : ""
                        color: Theme.fg
                        wrapMode: Text.Wrap; maximumLineCount: 5; elide: Text.ElideRight
                        font { family: Theme.font; pixelSize: 15; bold: true }
                    }
                    Text {
                        width: parent.width
                        text: cell.tab && cell.tab.url.toString() ? new URL(cell.tab.url.toString()).hostname : ""
                        color: Theme.muted; elide: Text.ElideRight
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
                MouseArea { anchors.fill: parent; onClicked: view.choose(cell.index) }
                Text {
                    anchors { right: parent.right; top: parent.top; margins: 10 }
                    text: "󰅖"; color: Theme.muted
                    font { family: Theme.font; pixelSize: 20 }
                    MouseArea { anchors { fill: parent; margins: -14 } onClicked: view.close(cell.index) }
                }
            }
        }
    }

    NumberAnimation { id: gridIn; target: grid; property: "opacity"; to: 1; duration: view.zoomDuration; easing.type: Easing.OutCubic }
    NumberAnimation { id: gridOut; target: grid; property: "opacity"; to: 0; duration: view.zoomDuration; easing.type: Easing.InCubic }

    // a page's picture between the screen and its card
    Image {
        id: zoom
        visible: false
        z: 10
        fillMode: Image.PreserveAspectCrop
        verticalAlignment: Image.AlignTop
        smooth: true
        clip: true
        property var then: null
        function place(r) {
            zoomAnim.stop()
            x = r.x; y = r.y; width = r.width; height = r.height
        }
        function animateTo(r, done) {
            then = done
            toX.to = r.x; toY.to = r.y; toW.to = r.width; toH.to = r.height
            zoomAnim.restart()
        }
        ParallelAnimation {
            id: zoomAnim
            NumberAnimation { id: toX; target: zoom; property: "x"; duration: view.zoomDuration; easing.type: Easing.OutCubic }
            NumberAnimation { id: toY; target: zoom; property: "y"; duration: view.zoomDuration; easing.type: Easing.OutCubic }
            NumberAnimation { id: toW; target: zoom; property: "width"; duration: view.zoomDuration; easing.type: Easing.OutCubic }
            NumberAnimation { id: toH; target: zoom; property: "height"; duration: view.zoomDuration; easing.type: Easing.OutCubic }
            onFinished: {
                const done = zoom.then
                zoom.then = null
                if (done)
                    done()
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
            MouseArea { id: doneTap; anchors.fill: parent; onClicked: view.choose(view.current) }
        }
    }
}
