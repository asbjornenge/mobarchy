pragma ComponentBehavior: Bound

import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Quick settings: slides down from the status bar (StatusBar.qml drives it). Pages side by
// side: the overview (tiles), Wi-Fi (the networks in range) and Bluetooth (devices).
Sheet {
    id: quick

    required property Brightness brightness
    required property Keyboard keyboard
    required property SystemStatus system
    required property Power power
    required property Wifi wifi
    required property Rfkill rfkill
    required property Bluetooth bluetooth
    required property MediaPlayers media
    required property Notifications notifications
    required property Volume volume

    // A secured network without a saved passphrase was picked.
    signal passphraseNeeded(string ssid)

    travel: panel.height
    LayerShell.Window.scope: "mobarchy-quick"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    property int page: 0  // 0: overview, 1: Wi-Fi, 2: Bluetooth
    property string wifiError: ""
    property string bluetoothError: ""

    // Restart and power off take a second tap within a few seconds.
    property string confirming: ""
    function confirmOr(action, run) {
        if (confirming === action) {
            confirming = ""
            run()
        } else {
            confirming = action
            confirmTimeout.restart()
        }
    }
    Timer { id: confirmTimeout; interval: 3000; onTriggered: quick.confirming = "" }

    function showWifi() {
        wifiError = ""
        page = 1
        wifi.scan()
    }

    function showBluetooth() {
        bluetoothError = ""
        page = 2
    }
    // Look for devices while the Bluetooth page is in view.
    readonly property bool lookingForDevices: page === 2 && shown && rfkill.bluetoothEnabled
    onLookingForDevicesChanged: bluetooth.setDiscovery(lookingForDevices)

    function deviceIcon(icon) {
        if (icon.startsWith("audio-head")) return "󰋋"
        if (icon === "audio-card") return "󰓃"
        if (icon === "input-keyboard") return "󰌌"
        if (icon === "input-mouse") return "󰍽"
        if (icon === "input-gaming") return "󰊴"
        if (icon === "phone") return "󰏲"
        if (icon === "computer") return "󰟀"
        return "󰂯"
    }

    onOpening: {
        media.refreshPosition()
        brightness.refresh()
        system.refresh()
        confirming = ""
        page = 0
    }

    Connections {
        target: quick.wifi
        function onJoinFailed(ssid, reason) { quick.wifiError = ssid + ": " + reason }
    }
    Connections {
        target: quick.bluetooth
        function onFailed(name, reason) { quick.bluetoothError = (name ? name + ": " : "") + reason }
    }

    readonly property bool wide: width > 640

    component Tile: Rectangle {
        id: tile
        property string icon
        property string label
        property string detail
        property bool active: false
        property bool chevron: false
        // Not there yet on this device: shown, but dimmed and inert.
        property bool supported: true
        signal clicked()
        width: (tiles.width - tiles.spacing * (tiles.columns - 1)) / tiles.columns
        height: 68
        radius: 14
        opacity: supported ? 1 : 0.45
        color: active ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : Theme.panel
        border.color: active ? Theme.accent : Theme.line
        scale: tap.pressed ? 0.96 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        MouseArea { id: tap; anchors.fill: parent; enabled: tile.supported; onClicked: tile.clicked() }
        Text {
            id: tileIcon
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: tile.icon
            color: tile.active ? Theme.accent : Theme.fg
            font { family: Theme.font; pixelSize: 22 }
        }
        Column {
            anchors { left: tileIcon.right; leftMargin: 14; right: more.left; rightMargin: 4
                      verticalCenter: parent.verticalCenter }
            spacing: 2
            Text {
                width: parent.width
                text: tile.label
                elide: Text.ElideRight
                color: Theme.fg
                font { family: Theme.font; pixelSize: 13; bold: true }
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: tile.detail
                elide: Text.ElideRight
                color: Theme.muted
                font { family: Theme.font; pixelSize: 11 }
            }
        }
        Text {
            id: more
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            text: tile.chevron ? "\uf054" : ""  // nf-fa-chevron_right
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
    }

    component RoundButton: Rectangle {
        id: rb
        property string icon
        property string label
        property bool warn: false
        signal clicked()
        width: Math.max(52, row.implicitWidth + 32); height: 44
        radius: 22
        color: warn ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.18) : Theme.panel
        border.color: warn ? Theme.red : Theme.line
        scale: rbTap.pressed ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Text { text: rb.icon; color: rb.warn ? Theme.red : Theme.fg; font { family: Theme.font; pixelSize: 18 } }
            Text {
                visible: rb.label.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: rb.label
                color: rb.warn ? Theme.red : Theme.fg
                font { family: Theme.font; pixelSize: 12 }
            }
        }
        MouseArea {
            id: rbTap; anchors { fill: parent; margins: -4 }
            onClicked: rb.clicked()
        }
    }

    // Tapping outside the panel closes it.
    Rectangle {
        anchors.fill: parent
        color: "black"
        opacity: 0.45 * quick.openness
        MouseArea { anchors.fill: parent; onClicked: quick.close() }
    }

    Rectangle {
        id: panel
        width: parent.width
        height: [overview, wifiPage, bluetoothPage][quick.page].implicitHeight + 28 + handle.height
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        y: -(1 - quick.openness) * height
        radius: 18
        color: Theme.bg
        border.color: Theme.line
        clip: true

        MouseArea { anchors.fill: parent }  // taps on the panel stay on the panel

        Item {
            id: pages
            anchors { top: parent.top; topMargin: 24; bottom: handle.top }
            width: parent.width
            x: -quick.page * width
            Behavior on x { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

            // --- overview ---------------------------------------------------------------
            Column {
                id: overview
                x: 20
                width: pages.width - 40
                spacing: 18

                // Time, date and battery, Omarchy style.
                Item {
                    width: parent.width
                    height: 48
                    Column {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        Text {
                            text: Qt.formatTime(clock.now, "HH:mm")
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 26; bold: true }
                        }
                        Text {
                            text: clock.now.toLocaleDateString(Qt.locale(), "dddd d MMMM")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 12 }
                        }
                    }
                    Column {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        visible: quick.system.battery >= 0
                        Text {
                            anchors.right: parent.right
                            text: (quick.system.charging ? "󰂄 " : "󰁹 ") + quick.system.battery + "%"
                            color: quick.system.battery <= 15 && !quick.system.charging ? Theme.red : Theme.fg
                            font { family: Theme.font; pixelSize: 18 }
                        }
                        Text {
                            anchors.right: parent.right
                            text: quick.system.charging ? qsTr("Charging") : qsTr("Battery")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 12 }
                        }
                    }
                    QtObject {
                        id: clock
                        property date now: new Date()
                    }
                    Timer {
                        interval: 1000
                        running: quick.shown
                        repeat: true
                        triggeredOnStart: true
                        onTriggered: clock.now = new Date()
                    }
                }

                LevelSlider {
                    width: parent.width
                    visible: quick.brightness.available
                    icon: "󰃠"
                    value: quick.brightness.level
                    onMoved: (v) => quick.brightness.level = v
                }
                LevelSlider {
                    width: parent.width
                    visible: quick.volume.available
                    icon: quick.volume.muted || quick.volume.volume === 0 ? "󰖁"
                        : quick.volume.volume < 0.34 ? "󰕿" : quick.volume.volume < 0.67 ? "󰖀" : "󰕾"
                    iconActive: quick.volume.muted
                    value: quick.volume.muted ? 0 : quick.volume.volume
                    onMoved: (v) => quick.volume.volume = v
                    onIconClicked: quick.volume.muted = !quick.volume.muted
                }

                NowPlaying {
                    width: parent.width
                    player: quick.media
                }

                Grid {
                    id: tiles
                    width: parent.width
                    columns: quick.wide ? 3 : 2
                    spacing: 12

                    Tile {
                        icon: !quick.rfkill.wifiEnabled ? "󰤮" : quick.wifi.ssid ? "󰤨" : "󰤯"
                        label: qsTr("Wi-Fi")
                        detail: !quick.rfkill.wifiEnabled ? qsTr("Off")
                              : quick.wifi.ssid || (quick.wifi.connecting ? qsTr("Connecting …") : qsTr("Not connected"))
                        active: quick.rfkill.wifiEnabled && quick.wifi.ssid.length > 0
                        chevron: true
                        supported: quick.rfkill.hasWifi || quick.wifi.available
                        onClicked: quick.showWifi()
                    }
                    Tile {
                        icon: "󰀝"
                        label: qsTr("Flight mode")
                        detail: quick.rfkill.flightMode ? qsTr("On") : qsTr("Off")
                        active: quick.rfkill.flightMode
                        supported: quick.rfkill.available
                        onClicked: quick.rfkill.flightMode = !quick.rfkill.flightMode
                    }
                    Tile {
                        icon: "󰌌"
                        label: qsTr("Keyboard")
                        detail: quick.keyboard.visible ? qsTr("Shown") : qsTr("Hidden")
                        active: quick.keyboard.visible
                        onClicked: quick.keyboard.toggle()
                    }
                    Tile {
                        icon: "󰒢"
                        label: qsTr("Mobile network")
                        detail: qsTr("Not supported yet")
                        supported: false
                    }
                    Tile {
                        icon: quick.volume.muted ? "󰖁" : "󰕾"
                        label: qsTr("Sound")
                        detail: !quick.volume.available ? qsTr("No audio output")
                              : quick.volume.muted ? qsTr("Muted")
                              : Math.round(quick.volume.volume * 100) + " %"
                        active: quick.volume.available && !quick.volume.muted
                        supported: quick.volume.available
                        onClicked: quick.volume.muted = !quick.volume.muted
                    }
                    Tile {
                        icon: !quick.rfkill.bluetoothEnabled ? "󰂲" : quick.bluetooth.connectedNames.length > 0 ? "󰂱" : "󰂯"
                        label: qsTr("Bluetooth")
                        detail: !quick.rfkill.bluetoothEnabled ? qsTr("Off")
                              : quick.bluetooth.connectedNames.join(", ") || qsTr("On")
                        active: quick.rfkill.bluetoothEnabled && quick.bluetooth.connectedNames.length > 0
                        chevron: true
                        supported: quick.rfkill.hasBluetooth || quick.bluetooth.available
                        onClicked: quick.showBluetooth()
                    }
                }

                // Notifications
                Item {
                    width: parent.width
                    height: 24
                    visible: quick.notifications.count > 0
                    Text {
                        anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                        text: qsTr("Notifications")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 12 }
                    }
                    Text {
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: qsTr("Clear all")
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 12 }
                        MouseArea {
                            anchors { fill: parent; margins: -10 }
                            onClicked: quick.notifications.clear()
                        }
                    }
                }
                ListView {
                    id: notificationList
                    width: parent.width
                    visible: count > 0
                    height: Math.min(contentHeight, 300)
                    clip: true
                    spacing: 8
                    currentIndex: -1  // see Home.qml
                    highlightFollowsCurrentItem: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: quick.notifications
                    delegate: Rectangle {
                        id: note
                        required property int notificationId
                        required property string app
                        required property string icon
                        required property string summary
                        required property string body
                        required property var actions
                        required property int urgency
                        required property date time
                        width: ListView.view.width
                        height: noteColumn.implicitHeight + 24
                        radius: 14
                        color: noteTap.pressed ? Theme.line : Theme.panel
                        border.color: urgency >= 2 ? Theme.red : "transparent"

                        MouseArea {
                            id: noteTap
                            anchors.fill: parent
                            onClicked: {
                                quick.notifications.invoke(note.notificationId, "default")
                                quick.close()
                            }
                        }
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
                                visible: note.actions.length > 0
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
                                            onClicked: quick.notifications.invoke(note.notificationId, actionButton.modelData.key)
                                        }
                                    }
                                }
                            }
                        }
                        MouseArea {
                            id: closeNote
                            anchors { right: parent.right; top: parent.top }
                            width: 44; height: 44
                            onClicked: quick.notifications.dismiss(note.notificationId)
                            Text {
                                anchors.centerIn: parent
                                text: "󰅖"
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 14 }
                            }
                        }
                    }
                }

                // System actions, as Omarchy's system menu.
                Row {
                    anchors.right: parent.right
                    spacing: 10
                    RoundButton {
                        icon: "󰜉"
                        label: quick.confirming === "reboot" ? qsTr("Tap to restart") : ""
                        warn: quick.confirming === "reboot"
                        onClicked: quick.confirmOr("reboot", () => quick.power.reboot())
                    }
                    RoundButton {
                        icon: "󰐥"
                        label: quick.confirming === "poweroff" ? qsTr("Tap to power off") : ""
                        warn: quick.confirming === "poweroff"
                        onClicked: quick.confirmOr("poweroff", () => quick.power.powerOff())
                    }
                }
            }

            // --- Wi-Fi ------------------------------------------------------------------
            Column {
                id: wifiPage
                x: pages.width + 20
                width: pages.width - 40
                spacing: 14

                Item {
                    width: parent.width
                    height: 40
                    MouseArea {
                        id: back
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: backRow.implicitWidth + 16
                        onClicked: quick.page = 0
                        Row {
                            id: backRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Text { text: "\uf053"; color: Theme.muted;  // nf-fa-chevron_left
                                   font { family: Theme.font; pixelSize: 16 }
                                   anchors.verticalCenter: parent.verticalCenter }
                            Text { text: qsTr("Wi-Fi"); color: Theme.fg
                                   font { family: Theme.font; pixelSize: 18; bold: true } }
                        }
                    }
                    Text {
                        anchors { right: wifiSwitch.left; rightMargin: 18; verticalCenter: parent.verticalCenter }
                        visible: quick.rfkill.wifiEnabled
                        text: quick.wifi.scanning ? "󰑓 " + qsTr("Scanning") : "󰑓"
                        color: quick.wifi.scanning ? Theme.accent : Theme.muted
                        font { family: Theme.font; pixelSize: 14 }
                        MouseArea {
                            anchors { fill: parent; margins: -12 }
                            onClicked: quick.wifi.scan()
                        }
                    }
                    Switch {
                        id: wifiSwitch
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        checked: quick.rfkill.wifiEnabled
                        onToggled: quick.rfkill.wifiEnabled = !quick.rfkill.wifiEnabled
                    }
                }

                Text {
                    width: parent.width
                    visible: quick.wifiError.length > 0
                    text: "✗ " + quick.wifiError
                    wrapMode: Text.Wrap
                    color: Theme.red
                    font { family: Theme.font; pixelSize: 12 }
                }

                Text {
                    visible: !quick.rfkill.wifiEnabled
                    text: quick.rfkill.flightMode ? qsTr("Flight mode is on.") : qsTr("Wi-Fi is off.")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }

                ListView {
                    id: networks
                    visible: quick.rfkill.wifiEnabled
                    width: parent.width
                    height: Math.min(contentHeight, quick.height * 0.55)
                    clip: true
                    spacing: 6
                    currentIndex: -1  // see Home.qml
                    highlightFollowsCurrentItem: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: quick.wifi.networks
                    delegate: Rectangle {
                        id: net
                        required property var modelData
                        readonly property bool joining: quick.wifi.connecting === modelData.ssid
                        width: ListView.view.width
                        height: 56
                        radius: 12
                        color: modelData.connected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                                                   : (rowTap.pressed ? Theme.line : Theme.panel)
                        border.color: modelData.connected ? Theme.accent : "transparent"

                        MouseArea {
                            id: rowTap
                            anchors.fill: parent
                            onClicked: {
                                quick.wifiError = ""
                                if (net.modelData.connected)
                                    return
                                if (!quick.wifi.join(net.modelData.ssid))
                                    quick.passphraseNeeded(net.modelData.ssid)
                            }
                        }
                        Text {
                            id: bars
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            text: ["󰤯", "󰤟", "󰤢", "󰤥", "󰤨"][net.modelData.bars]
                            color: net.modelData.connected ? Theme.accent : Theme.fg
                            font { family: Theme.font; pixelSize: 20 }
                        }
                        Column {
                            anchors { left: bars.right; leftMargin: 14; right: trailing.left; rightMargin: 8
                                      verticalCenter: parent.verticalCenter }
                            Text {
                                width: parent.width
                                text: net.modelData.ssid
                                elide: Text.ElideRight
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 14 }
                            }
                            Text {
                                width: parent.width
                                visible: text.length > 0
                                text: net.joining ? qsTr("Connecting …")
                                    : net.modelData.connected ? qsTr("Connected")
                                    : net.modelData.known ? qsTr("Saved") : ""
                                color: net.modelData.connected || net.joining ? Theme.accent : Theme.muted
                                font { family: Theme.font; pixelSize: 11 }
                            }
                        }
                        Row {
                            id: trailing
                            anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                            spacing: 12
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: net.modelData.secured
                                text: "󰌾"
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 14 }
                            }
                            // Forget a saved network.
                            Rectangle {
                                visible: net.modelData.known
                                width: forgetText.implicitWidth + 20; height: 30; radius: 15
                                color: "transparent"
                                // A second tap confirms: the passphrase is gone afterwards.
                                readonly property bool confirm: quick.confirming === "forget:" + net.modelData.ssid
                                border.color: confirm ? Theme.red : Theme.line
                                Text {
                                    id: forgetText
                                    anchors.centerIn: parent
                                    text: parent.confirm ? qsTr("Tap to forget") : qsTr("Forget")
                                    color: parent.confirm ? Theme.red : Theme.muted
                                    font { family: Theme.font; pixelSize: 11 }
                                }
                                MouseArea {
                                    anchors { fill: parent; margins: -6 }
                                    onClicked: quick.confirmOr("forget:" + net.modelData.ssid,
                                                               () => quick.wifi.forget(net.modelData.ssid))
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: quick.rfkill.wifiEnabled && networks.count === 0
                    text: quick.wifi.scanning ? qsTr("Looking for networks …") : qsTr("No networks found.")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
            }

            // --- Bluetooth --------------------------------------------------------------
            Column {
                id: bluetoothPage
                x: 2 * pages.width + 20
                width: pages.width - 40
                spacing: 14

                // Paired devices, then the ones around, each list under its heading.
                readonly property var rows: {
                    const paired = quick.bluetooth.devices.filter(d => d.paired)
                    const around = quick.bluetooth.devices.filter(d => !d.paired)
                    let rows = []
                    if (paired.length > 0)
                        rows = rows.concat([{ heading: qsTr("My devices") }], paired)
                    if (around.length > 0)
                        rows = rows.concat([{ heading: qsTr("Available") }], around)
                    return rows
                }

                Item {
                    width: parent.width
                    height: 40
                    MouseArea {
                        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                        width: btBackRow.implicitWidth + 16
                        onClicked: quick.page = 0
                        Row {
                            id: btBackRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Text { text: "\uf053"; color: Theme.muted;  // nf-fa-chevron_left
                                   font { family: Theme.font; pixelSize: 16 }
                                   anchors.verticalCenter: parent.verticalCenter }
                            Text { text: qsTr("Bluetooth"); color: Theme.fg
                                   font { family: Theme.font; pixelSize: 18; bold: true } }
                        }
                    }
                    Text {
                        anchors { right: bluetoothSwitch.left; rightMargin: 18; verticalCenter: parent.verticalCenter }
                        visible: quick.bluetooth.discovering
                        text: "󰑓 " + qsTr("Searching")
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    Switch {
                        id: bluetoothSwitch
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        checked: quick.rfkill.bluetoothEnabled
                        onToggled: quick.rfkill.bluetoothEnabled = !quick.rfkill.bluetoothEnabled
                    }
                }

                Text {
                    width: parent.width
                    visible: quick.bluetoothError.length > 0
                    text: "✗ " + quick.bluetoothError
                    wrapMode: Text.Wrap
                    color: Theme.red
                    font { family: Theme.font; pixelSize: 12 }
                }

                Text {
                    width: parent.width
                    visible: !quick.rfkill.bluetoothEnabled || !quick.bluetooth.available
                    text: quick.rfkill.flightMode ? qsTr("Flight mode is on.")
                        : !quick.rfkill.bluetoothEnabled ? qsTr("Bluetooth is off.")
                        : qsTr("Starting Bluetooth …")
                    wrapMode: Text.Wrap
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }

                ListView {
                    id: bluetoothDevices
                    visible: quick.rfkill.bluetoothEnabled && quick.bluetooth.available
                    width: parent.width
                    height: Math.min(contentHeight, quick.height * 0.55)
                    clip: true
                    spacing: 6
                    currentIndex: -1  // see Home.qml
                    highlightFollowsCurrentItem: false
                    boundsBehavior: Flickable.StopAtBounds
                    model: bluetoothPage.rows
                    delegate: Item {
                        id: dev
                        required property var modelData
                        readonly property bool heading: modelData.heading !== undefined
                        width: ListView.view.width
                        height: heading ? 30 : 56

                        Text {
                            visible: dev.heading
                            anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 4 }
                            text: dev.modelData.heading ?? ""
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 12 }
                        }
                        Rectangle {
                            visible: !dev.heading
                            anchors.fill: parent
                            radius: 12
                            color: dev.modelData.connected ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.12)
                                                           : (devTap.pressed ? Theme.line : Theme.panel)
                            border.color: dev.modelData.connected ? Theme.accent : "transparent"

                            MouseArea {
                                id: devTap
                                anchors.fill: parent
                                onClicked: {
                                    quick.bluetoothError = ""
                                    quick.bluetooth.activate(dev.modelData.path)
                                }
                            }
                            Text {
                                id: devIcon
                                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                                text: quick.deviceIcon(dev.modelData.icon ?? "")
                                color: dev.modelData.connected ? Theme.accent : Theme.fg
                                font { family: Theme.font; pixelSize: 20 }
                            }
                            Column {
                                anchors { left: devIcon.right; leftMargin: 14; right: devTrailing.left; rightMargin: 8
                                          verticalCenter: parent.verticalCenter }
                                Text {
                                    width: parent.width
                                    text: dev.modelData.name ?? ""
                                    elide: Text.ElideRight
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 14 }
                                }
                                Text {
                                    width: parent.width
                                    text: dev.modelData.busy ? (dev.modelData.paired ? qsTr("Connecting …") : qsTr("Pairing …"))
                                        : dev.modelData.connected ? qsTr("Connected")
                                              + (dev.modelData.battery >= 0 ? "  ·  󰁹 " + dev.modelData.battery + " %" : "")
                                        : dev.modelData.paired ? qsTr("Not connected") : qsTr("Tap to pair")
                                    color: dev.modelData.connected || dev.modelData.busy ? Theme.accent : Theme.muted
                                    font { family: Theme.font; pixelSize: 11 }
                                }
                            }
                            // Forget a paired device.
                            Rectangle {
                                id: devTrailing
                                anchors { right: parent.right; rightMargin: 12; verticalCenter: parent.verticalCenter }
                                visible: dev.modelData.paired === true
                                width: visible ? devForgetText.implicitWidth + 20 : 0; height: 30; radius: 15
                                color: "transparent"
                                readonly property bool confirm: quick.confirming === "forget-bt:" + dev.modelData.path
                                border.color: confirm ? Theme.red : Theme.line
                                Text {
                                    id: devForgetText
                                    anchors.centerIn: parent
                                    text: parent.confirm ? qsTr("Tap to forget") : qsTr("Forget")
                                    color: parent.confirm ? Theme.red : Theme.muted
                                    font { family: Theme.font; pixelSize: 11 }
                                }
                                MouseArea {
                                    anchors { fill: parent; margins: -6 }
                                    onClicked: quick.confirmOr("forget-bt:" + dev.modelData.path,
                                                               () => quick.bluetooth.forget(dev.modelData.path))
                                }
                            }
                        }
                    }
                }

                Text {
                    visible: quick.rfkill.bluetoothEnabled && quick.bluetooth.available && bluetoothDevices.count === 0
                    text: quick.bluetooth.discovering ? qsTr("Looking for devices …") : qsTr("No devices found.")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
            }
        }

        // Dragging the handle up closes the panel.
        EdgeGesture {
            id: handle
            direction: -1
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 36
            onBegan: quick.beginDrag()
            onMoved: (distance) => quick.dragTo(distance)
            onEnded: (velocity) => quick.endDrag(velocity)
            onTapped: quick.close()
            Rectangle {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 12 }
                width: 44; height: 5; radius: 2.5
                color: Theme.muted
            }
        }
    }
}
