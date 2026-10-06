import QtQuick

// The menu: a sheet from the bottom with big buttons. Tapping outside closes it.
Item {
    id: menu
    property bool canGoForward
    property bool desktopSite
    property real textScale: 1
    property bool passwords          // 1Password is there
    signal logins()
    signal forward()
    signal reload()
    signal newTab()
    signal copyLink()
    signal toggleDesktop()
    signal textSmaller()
    signal textBigger()

    visible: false
    function open() { visible = true }
    function close() { visible = false }

    Rectangle {
        anchors.fill: parent
        color: "#80000000"
        MouseArea { anchors.fill: parent; onClicked: menu.close() }
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: grid.height + 40
        radius: 20
        color: Theme.panel
        MouseArea { anchors.fill: parent }  // (taps on the sheet stay here)
        Grid {
            id: grid
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 20 }
            columns: 4
            columnSpacing: Math.max(8, (menu.width - 4 * 72 - 24) / 3)
            rowSpacing: 12
            BarButton { width: 72; icon: "󰁔"; label: qsTr("Forward"); enabled_: menu.canGoForward
                        onClicked: { menu.close(); menu.forward() } }
            BarButton { width: 72; icon: "󰑐"; label: qsTr("Reload"); onClicked: { menu.close(); menu.reload() } }
            BarButton { width: 72; icon: "󰐕"; label: qsTr("New tab"); onClicked: { menu.close(); menu.newTab() } }
            BarButton { width: 72; icon: "󰆏"; label: qsTr("Copy link"); onClicked: { menu.close(); menu.copyLink() } }
            BarButton { width: 72; icon: menu.desktopSite ? "󰄜" : "󰍹"
                        label: menu.desktopSite ? qsTr("Phone site") : qsTr("Desktop site")
                        onClicked: { menu.close(); menu.toggleDesktop() } }
            BarButton { width: 72; icon: "A−"; label: qsTr("Smaller"); onClicked: menu.textSmaller() }
            BarButton { width: 72; icon: "A+"; label: qsTr("Bigger"); onClicked: menu.textBigger() }
            BarButton { width: 72; visible: menu.passwords; icon: "󰌋"; label: qsTr("1Password")
                        onClicked: { menu.close(); menu.logins() } }
            Item {
                width: 72; height: 64
                Text {
                    anchors.centerIn: parent
                    text: Math.round(menu.textScale * 100) + " %"
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
            }
        }
    }
}
