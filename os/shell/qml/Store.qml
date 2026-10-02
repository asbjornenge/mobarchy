pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The app store (ADR 0012): apps known to work on Mobarchy, native and web, by category.
// Its own window (mobarchy-shell --store), opened from the app list.
Window {
    id: win

    visible: true
    title: qsTr("Store")
    color: Theme.bg

    AppStore {
        id: store
        onFailed: (name, reason) => win.error = (name ? name + ": " : "") + reason
        onInstalled: (id) => win.error = ""
    }
    AppModel { id: launcher }

    property string error: ""
    property string category: ""   // "" is all
    property var selected: null     // the app whose page is open
    readonly property var shown: store.apps.filter(a =>
        (category === "" || a.category === category)
        && (search.text === "" || (a.name + " " + a.summary + " " + a.description).toLowerCase()
                                      .includes(search.text.toLowerCase())))

    function iconSource(a) {
        return !a.icon ? "" : a.icon.startsWith("/") ? "file://" + a.icon : a.icon
    }
    // The selected app as the store has it now (installed, busy).
    function current(id) {
        return store.apps.find(a => a.id === id) ?? null
    }

    component ActionButton: Rectangle {
        id: button
        required property var app
        readonly property string mode: !app ? "" : app.busy ? "busy" : app.installed ? "open" : "install"
        width: label.implicitWidth + 32
        height: 36
        radius: 18
        color: mode === "install" ? Theme.accent : "transparent"
        border.color: mode === "install" ? Theme.accent : Theme.line
        opacity: tap.pressed ? 0.7 : 1
        Text {
            id: label
            anchors.centerIn: parent
            text: button.mode === "busy" ? qsTr("Working …") : button.mode === "open" ? qsTr("Open") : qsTr("Install")
            color: button.mode === "install" ? Theme.bg : Theme.fg
            font { family: Theme.font; pixelSize: 13; bold: true }
        }
        MouseArea {
            id: tap
            anchors { fill: parent; margins: -6 }
            enabled: button.mode !== "busy"
            onClicked: {
                win.error = ""
                if (button.mode === "open")
                    launcher.launch(button.app.app_id)
                else
                    store.install(button.app.id)
            }
        }
    }

    component AppIcon: Item {
        id: appIcon
        required property var app
        property int size: 48
        width: size; height: size
        Image {
            id: img
            anchors.fill: parent
            source: win.iconSource(appIcon.app)
            sourceSize: Qt.size(appIcon.size * 2, appIcon.size * 2)
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            visible: status === Image.Ready
        }
        Rectangle {
            anchors.fill: parent
            visible: img.status !== Image.Ready
            radius: appIcon.size / 4
            color: Theme.panel
            Text {
                anchors.centerIn: parent
                text: appIcon.app.kind === "webapp" ? "󰖟" : "󰀻"
                color: Theme.muted
                font { family: Theme.font; pixelSize: appIcon.size / 2 }
            }
        }
    }

    // --- the list --------------------------------------------------------------------------
    Item {
        anchors.fill: parent
        visible: win.selected === null

        Column {
            id: top
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            spacing: 14

            Text {
                text: qsTr("Store")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 26; bold: true }
            }
            Text {
                width: parent.width
                text: qsTr("Apps that work well on Mobarchy. Anything else installs too: just ask the agent.")
                wrapMode: Text.Wrap
                color: Theme.muted
                font { family: Theme.font; pixelSize: 12 }
            }
            Rectangle {
                width: parent.width
                height: 44
                radius: 22
                color: Theme.panel
                border.color: search.activeFocus ? Theme.accent : Theme.line
                TextInput {
                    id: search
                    anchors { left: parent.left; right: parent.right; leftMargin: 18; rightMargin: 18
                              verticalCenter: parent.verticalCenter }
                    color: Theme.fg
                    clip: true
                    font { family: Theme.font; pixelSize: 14 }
                    Text {
                        visible: !search.text && !search.activeFocus
                        text: "󰍉  " + qsTr("Search")
                        color: Theme.muted
                        font: search.font
                    }
                }
            }
            ListView {
                width: parent.width
                height: 34
                orientation: ListView.Horizontal
                spacing: 8
                clip: true
                currentIndex: -1
                highlightFollowsCurrentItem: false
                boundsBehavior: Flickable.StopAtBounds
                model: [{ id: "", name: qsTr("All") }].concat(store.categories)
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    readonly property bool on: win.category === modelData.id
                    width: chipText.implicitWidth + 28
                    height: 34
                    radius: 17
                    color: on ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18) : Theme.panel
                    border.color: on ? Theme.accent : Theme.line
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: chip.modelData.name
                        color: chip.on ? Theme.accent : Theme.fg
                        font { family: Theme.font; pixelSize: 12 }
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.category = chip.modelData.id }
                }
            }
            Text {
                width: parent.width
                visible: win.error.length > 0
                text: "✗ " + win.error
                wrapMode: Text.Wrap
                color: Theme.red
                font { family: Theme.font; pixelSize: 12 }
            }
        }

        ListView {
            anchors { left: parent.left; right: parent.right; top: top.bottom; bottom: parent.bottom
                      topMargin: 12; leftMargin: 20; rightMargin: 20 }
            clip: true
            spacing: 8
            currentIndex: -1
            highlightFollowsCurrentItem: false
            boundsBehavior: Flickable.StopAtBounds
            model: win.shown
            footer: Item { width: 1; height: 24 }
            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 76
                radius: 14
                color: rowTap.pressed ? Theme.line : Theme.panel
                MouseArea { id: rowTap; anchors.fill: parent; onClicked: win.selected = row.modelData.id }
                AppIcon {
                    id: rowIcon
                    app: row.modelData
                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                }
                Column {
                    anchors { left: rowIcon.right; leftMargin: 14; right: rowAction.left; rightMargin: 10
                              verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Row {
                        spacing: 8
                        Text {
                            text: row.modelData.name
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 14; bold: true }
                        }
                        Text {
                            visible: row.modelData.kind === "webapp"
                            text: qsTr("web")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 10 }
                            anchors.baseline: parent.children[0].baseline
                        }
                    }
                    Text {
                        width: parent.width
                        text: row.modelData.summary
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 11 }
                    }
                }
                ActionButton {
                    id: rowAction
                    app: row.modelData
                    anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: store.loading ? qsTr("Loading …") : qsTr("Nothing found.")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }

    // --- an app's page ---------------------------------------------------------------------
    Flickable {
        id: page
        readonly property var app: win.selected === null ? null : win.current(win.selected)
        anchors.fill: parent
        visible: app !== null
        contentHeight: details.implicitHeight + 40
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        onVisibleChanged: contentY = 0

        Column {
            id: details
            x: 20
            y: 20
            width: page.width - 40
            spacing: 18

            MouseArea {
                width: backRow.implicitWidth + 16
                height: 36
                onClicked: win.selected = null
                Row {
                    id: backRow
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 10
                    Text { text: ""; color: Theme.muted; font { family: Theme.font; pixelSize: 16 }
                           anchors.verticalCenter: parent.verticalCenter }
                    Text { text: qsTr("Store"); color: Theme.muted; font { family: Theme.font; pixelSize: 14 } }
                }
            }
            Row {
                width: parent.width
                spacing: 16
                AppIcon {
                    id: bigIcon
                    app: page.app ?? {}
                    size: 72
                }
                Column {
                    width: parent.width - bigIcon.width - parent.spacing
                    anchors.verticalCenter: bigIcon.verticalCenter
                    spacing: 4
                    Text {
                        width: parent.width
                        text: page.app ? page.app.name : ""
                        wrapMode: Text.Wrap
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 20; bold: true }
                    }
                    Text {
                        width: parent.width
                        text: page.app ? page.app.summary : ""
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
            }
            Row {
                spacing: 12
                ActionButton { app: page.app }
                Rectangle {
                    visible: page.app !== null && page.app.installed && !page.app.busy
                    width: removeText.implicitWidth + 28; height: 36; radius: 18
                    color: "transparent"
                    readonly property bool confirm: win.confirmRemove === (page.app ? page.app.id : "")
                    border.color: confirm ? Theme.red : Theme.line
                    Text {
                        id: removeText
                        anchors.centerIn: parent
                        text: parent.confirm ? qsTr("Tap to remove") : qsTr("Remove")
                        color: parent.confirm ? Theme.red : Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    MouseArea {
                        anchors { fill: parent; margins: -6 }
                        onClicked: {
                            if (parent.confirm) {
                                win.confirmRemove = ""
                                store.remove(page.app.id)
                            } else {
                                win.confirmRemove = page.app.id
                                confirmTimeout.restart()
                            }
                        }
                    }
                }
            }
            Text {
                width: parent.width
                visible: page.app !== null
                text: !page.app ? ""
                    : (page.app.tested.length > 0 ? "✓ " + qsTr("Tested on this phone") : qsTr("Not tested on a phone yet"))
                      + "  ·  " + page.app.category_name
                      + (page.app.kind === "webapp" ? "  ·  " + qsTr("Web app") : "")
                color: page.app && page.app.tested.length > 0 ? Theme.accent : Theme.muted
                font { family: Theme.font; pixelSize: 12 }
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: page.app ? page.app.note : ""
                wrapMode: Text.Wrap
                color: Theme.fg
                font { family: Theme.font; pixelSize: 13; italic: true }
            }
            ListView {
                width: parent.width
                height: visible ? 240 : 0
                visible: page.app !== null && page.app.screenshots.length > 0
                orientation: ListView.Horizontal
                spacing: 10
                clip: true
                currentIndex: -1
                highlightFollowsCurrentItem: false
                model: page.app ? page.app.screenshots : []
                delegate: Rectangle {
                    id: shot
                    required property string modelData
                    height: 240
                    width: Math.max(120, shotImage.paintedWidth)
                    radius: 10
                    color: Theme.panel
                    clip: true
                    Image {
                        id: shotImage
                        height: parent.height
                        source: shot.modelData
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        sourceSize.height: 480
                    }
                }
            }
            Text {
                width: parent.width
                text: page.app ? (page.app.description || page.app.url || "") : ""
                wrapMode: Text.Wrap
                color: Theme.fg
                lineHeight: 1.3
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }

    property string confirmRemove: ""
    Timer { id: confirmTimeout; interval: 3000; onTriggered: win.confirmRemove = "" }
}
