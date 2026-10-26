pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Home's third page, a swipe left from the widgets: the installed apps (the open ones are in
// the switcher, AppSwitcher.qml), Mobarchy's own first, and a search the shell's keyboard
// types into. A tap goes to the app (its window, if it has one open); holding it offers a new
// window.
Item {
    id: page

    required property AppModel apps
    required property ToplevelModel toplevels
    required property Keyboard keyboard
    // room kept free at the bottom
    property real bottomInset: 0

    // --- the search: the keyboard types into it (Keyboard.capturing), the grid follows ---
    readonly property bool searching: keyboard.capturing
    function startSearch() {
        keyboard.startCapture()
        keyboard.visible = true
    }
    // A hardware keyboard: typing on the page searches (no on-screen keyboard for it), Return
    // starts the first match, Escape ends the search. (Home is a window with the keyboard's
    // focus; Home gives the page its focus while it is the page in view.)
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) {
            if (searching) { endSearch(); event.accepted = true }
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (searching) { keyboard.key("Return"); event.accepted = true }
        } else if (event.key === Qt.Key_Backspace) {
            if (searching) { keyboard.backspace(); event.accepted = true }
        } else if (event.text !== "" && event.text.charCodeAt(0) >= 32
                   && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
            if (!searching)
                keyboard.startCapture()
            keyboard.type(event.text)
            event.accepted = true
        }
    }
    function endSearch() {
        newWindow.visible = false
        if (keyboard.capturing) {
            keyboard.stopCapture()
            keyboard.visible = false
        }
    }
    Connections {
        target: page.keyboard
        // Return: the first match
        function onCaptureAccepted() {
            const id = own.first() || others.first()
            if (id !== "")
                page.open_(id)
        }
    }
    function open_(appId) {
        endSearch()
        if (!toplevels.activateApp(apps.windowIds(appId)))
            apps.launch(appId)
    }

    // Holding an app's icon: a new window of it, apart from the one it has open.
    function offerNewWindow(appId, name, tile) {
        const p = tile.mapToItem(page, tile.width / 2, 0)
        newWindow.appId = appId
        newWindow.name = name
        newWindow.x = Math.max(12, Math.min(page.width - newWindow.width - 12, p.x - newWindow.width / 2))
        newWindow.y = Math.max(12, p.y - newWindow.height - 6)
        newWindow.visible = true
    }
    // (a tap beside it: it goes)
    MouseArea {
        anchors.fill: parent
        enabled: newWindow.visible
        z: 9
        onClicked: newWindow.visible = false
    }

    // --- installed apps -------------------------------------------------------------
    Rectangle {
        id: search
        anchors { left: parent.left; right: parent.right; top: parent.top; leftMargin: 16; rightMargin: 16; topMargin: 20 }
        height: 42
        radius: 21
        color: Theme.panel
        border.color: page.searching ? Theme.accent : Theme.line
        // what is typed, the cursor after it; before anything is typed the cursor comes
        // first and the hint stays behind it, faint (nothing to rub out)
        Row {
            anchors { left: parent.left; right: clear.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            spacing: 0
            Text {
                visible: !page.searching
                text: "󰍉  "
                color: Theme.muted
                font { family: Theme.font; pixelSize: 15 }
            }
            Text {
                text: page.keyboard.captured
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
            }
            Rectangle {
                visible: page.searching
                anchors.verticalCenter: parent.verticalCenter
                width: 2; height: 20
                color: Theme.accent
                SequentialAnimation on opacity {
                    running: page.searching
                    loops: Animation.Infinite
                    NumberAnimation { to: 0; duration: 500; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1; duration: 500; easing.type: Easing.InOutQuad }
                }
            }
            Text {
                visible: page.keyboard.captured === ""
                leftPadding: page.searching ? 4 : 0
                text: qsTr("Search apps")
                color: Theme.muted
                opacity: page.searching ? 0.6 : 1
                font { family: Theme.font; pixelSize: 15 }
            }
        }
        Text {
            id: clear
            visible: page.searching
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            text: "󰅖"
            color: Theme.muted
            font { family: Theme.font; pixelSize: 16 }
        }
        MouseArea {
            anchors.fill: parent
            onClicked: page.searching ? page.endSearch() : page.startSearch()
        }
    }
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; top: search.bottom; topMargin: 40 }
        visible: page.searching && own.count + others.count === 0
        text: qsTr("No apps called “%1”").arg(page.keyboard.captured)
        color: Theme.muted
        font { family: Theme.font; pixelSize: 14 }
    }
    // Mobarchy's own apps first, a line, then the rest
    component AppGrid: Grid {
        id: appGrid
        required property AppFilter apps
        width: parent.width
        columns: Math.max(4, Math.floor(width / 120))
        readonly property real cell: width / columns
        Repeater {
            model: appGrid.apps
            delegate: Item {
                id: tile
                required property string appId
                required property string name
                required property string icon
                width: appGrid.cell; height: 104

                // A tap goes to the app (its window, if it has one open); holding it offers a
                // new window.
                MouseArea {
                    id: tap
                    anchors.fill: parent
                    onClicked: page.open_(tile.appId)
                    onPressAndHold: page.offerNewWindow(tile.appId, tile.name, tile)
                }
                Column {
                    anchors.centerIn: parent
                    width: parent.width - 8
                    spacing: 8
                    scale: tap.pressed ? 0.9 : 1
                    Behavior on scale { NumberAnimation { duration: 90 } }
                    Image {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 52; height: 52
                        sourceSize: Qt.size(104, 104)
                        asynchronous: true
                        source: "image://icon/" + tile.icon
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: tile.name
                        elide: Text.ElideRight
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
            }
        }
    }
    AppFilter { id: own; source: page.apps; query: page.keyboard.captured; group: "own" }
    AppFilter { id: others; source: page.apps; query: page.keyboard.captured; group: "others" }
    Flickable {
        id: grid
        maximumFlickVelocity: Theme.flickVelocity
        boundsBehavior: Theme.boundsBehavior
        flickDeceleration: Theme.flickDeceleration
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
        anchors {
            left: parent.left; right: parent.right; bottom: parent.bottom
            top: search.bottom
            topMargin: 8; leftMargin: 8; rightMargin: 8; bottomMargin: page.bottomInset
        }
        clip: true
        contentHeight: sections.height + 24
        Column {
            id: sections
            width: grid.width
            spacing: 6
            AppGrid { apps: own }
            Rectangle {
                visible: own.count > 0 && others.count > 0
                x: 16; width: parent.width - 32; height: 1
                color: Theme.line
            }
            AppGrid { apps: others }
        }
    }

    // (offerNewWindow) A new window; for a web app, removing it too.
    Rectangle {
        id: newWindow
        property string appId
        property string name
        readonly property bool webApp: appId !== "" && page.apps.isWebApp(appId)
        visible: false
        z: 10
        width: newRow.implicitWidth + 36; height: 52; radius: 26
        color: newTap.pressed ? Theme.line : Theme.panel
        border.color: Theme.accent
        Row {
            id: newRow
            anchors.centerIn: parent
            spacing: 10
            Text { text: "󰖯"; color: Theme.accent; font { family: Theme.font; pixelSize: 18 }
                   anchors.verticalCenter: parent.verticalCenter }
            Text { text: qsTr("New window"); color: Theme.fg; font { family: Theme.font; pixelSize: 14 }
                   anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea {
            id: newTap
            anchors.fill: parent
            onClicked: {
                newWindow.visible = false
                page.apps.launch(newWindow.appId)
                page.endSearch()
            }
        }
        Rectangle {
            visible: newWindow.webApp
            anchors { top: parent.bottom; topMargin: 8; horizontalCenter: parent.horizontalCenter }
            width: removeRow.implicitWidth + 36; height: 52; radius: 26
            color: removeTap.pressed ? Theme.line : Theme.panel
            border.color: Theme.red
            Row {
                id: removeRow
                anchors.centerIn: parent
                spacing: 10
                Text { text: "󰆴"; color: Theme.red; font { family: Theme.font; pixelSize: 18 }
                       anchors.verticalCenter: parent.verticalCenter }
                Text { text: qsTr("Remove %1").arg(newWindow.name); color: Theme.fg
                       font { family: Theme.font; pixelSize: 14 }
                       anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea {
                id: removeTap
                anchors.fill: parent
                onClicked: {
                    newWindow.visible = false
                    page.apps.removeWebApp(newWindow.appId)
                }
            }
        }
    }
}
