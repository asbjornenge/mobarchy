import QtQuick

// The browser's one bar, at the bottom for the thumb: back, the page (tap to edit the
// address), the tabs and the menu. A swipe along it goes back (right) or forward (left).
Rectangle {
    id: bar
    property string title
    property string host
    property bool secure
    property real progress: 1        // loading: 0..1
    property bool canGoBack
    property int tabCount
    signal back()
    signal forward()
    signal editAddress()
    signal showTabs()
    signal showMenu()

    height: 64
    color: Theme.bgDark
    Rectangle { width: parent.width; height: 1; color: Theme.line }

    // loading
    Rectangle {
        visible: bar.progress < 1
        width: parent.width * bar.progress; height: 2
        color: Theme.accent
        Behavior on width { NumberAnimation { duration: 150 } }
    }

    Row {
        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
        spacing: 4

        BarButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "󰁍"
            enabled_: bar.canGoBack
            onClicked: bar.back()
        }

        // the page: big and readable; a tap edits the address
        Rectangle {
            id: pill
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 3 * 52 - 3 * parent.spacing
            height: 46
            radius: 23
            color: pillTap.pressed ? Theme.line : Theme.panel
            Row {
                anchors { left: parent.left; leftMargin: 16; right: parent.right; rightMargin: 14
                          verticalCenter: parent.verticalCenter }
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: bar.secure ? "󰌾" : "󰞀"
                    color: bar.secure ? Theme.muted : Theme.red
                    font { family: Theme.font; pixelSize: 15 }
                }
                Text {
                    width: parent.width - 24
                    anchors.verticalCenter: parent.verticalCenter
                    text: bar.host || bar.title || qsTr("Search or address")
                    color: bar.host ? Theme.fg : Theme.muted
                    elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 16 }
                }
            }
            MouseArea { id: pillTap; anchors.fill: parent; onClicked: bar.editAddress() }
        }

        // the tabs: how many
        Item {
            width: 52; height: 52
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                anchors.centerIn: parent
                width: 26; height: 26; radius: 6
                color: tabsTap.pressed ? Theme.line : "transparent"
                border { color: Theme.fg; width: 2 }
                Text {
                    anchors.centerIn: parent
                    text: bar.tabCount > 99 ? "∞" : bar.tabCount
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 13; bold: true }
                }
            }
            MouseArea { id: tabsTap; anchors.fill: parent; onClicked: bar.showTabs() }
        }

        BarButton {
            anchors.verticalCenter: parent.verticalCenter
            icon: "󰇙"
            onClicked: bar.showMenu()
        }
    }

    // a swipe along the bar: back (to the right) or forward (to the left)
    DragHandler {
        id: swipe
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        onActiveChanged: {
            if (active)
                return
            const dx = centroid.position.x - centroid.pressPosition.x
            if (dx > 80)
                bar.back()
            else if (dx < -80)
                bar.forward()
        }
    }
}
