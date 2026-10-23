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
    required property Telephony telephony
    // flight (bool) and mobile (bool), kept across restarts (Main)
    required property var radioSettings
    // the screen's settings (Main: timeout, seconds without a touch before it goes dark; 0 never)
    required property var screenSettings
    required property Bluetooth bluetooth
    required property MediaPlayers media
    required property PinLock pinLock
    required property Notifications notifications
    required property Volume volume
    required property AppModel apps
    required property Torch torch
    required property Rotation rotation
    required property Haptics haptics
    required property Fingerprint fingerprint

    // A secured network without a saved passphrase was picked.
    signal passphraseNeeded(string ssid)

    travel: panel.height
    LayerShell.Window.scope: "mobarchy-quick"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    property int page: 0  // 0: overview, 1: Wi-Fi, 2: Bluetooth, 3: screen lock (PIN, fingers),
                          // 4: notifications, 5: hotspot, 6: settings
    // sharing the internet over Wi-Fi (ADR 0039): followed while the panel is open
    readonly property Hotspot hotspot: Hotspot { watching: quick.shown }
    property bool renamingHotspot: false
    function startRenamingHotspot() {
        renamingHotspot = true
        keyboard.startCapture()
        keyboard.visible = true
    }
    function renameHotspot() {
        const name = keyboard.captured.trim()
        renamingHotspot = false
        keyboard.stopCapture()
        keyboard.visible = false
        if (name !== "" && name !== hotspot.name)
            hotspot.configure(name, hotspot.password)
    }
    Connections {
        target: quick.keyboard
        function onCaptureAccepted() { if (quick.renamingHotspot) quick.renameHotspot() }
    }

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

    // Setting, changing or removing the PIN, one pad at a time.
    property string pinAction: ""   // set, change, remove
    property string pinStep: ""     // old, new, confirm
    property string pinOld: ""
    property string pinNew: ""
    function showLock() {
        pinAction = ""
        pinStep = ""
        lockPad.message = ""
        page = 3
    }
    function startPin(action) {
        if (action === "finger")
            fingerResult = ""
        pinAction = action
        pinOld = ""
        pinNew = ""
        lockPad.message = ""
        lockPad.messageIsError = false
        pinStep = action === "set" ? "new" : "old"
        lockPad.clear()
    }
    function pinEntered(pin) {
        lockPad.clear()
        lockPad.messageIsError = false
        lockPad.message = ""
        if (pinStep === "old") {
            pinOld = pin
            if (pinAction === "finger")
                pinLock.verify(pin)
            else if (pinAction === "remove")
                pinLock.remove(pin)
            else
                pinStep = "new"
        } else if (pinStep === "new") {
            if (pin.length < 4) {
                lockPad.messageIsError = true
                lockPad.message = qsTr("At least 4 digits")
                return
            }
            pinNew = pin
            pinStep = "confirm"
        } else if (pinStep === "confirm") {
            if (pin !== pinNew) {
                lockPad.messageIsError = true
                lockPad.message = qsTr("The PINs differ: try again")
                lockPad.shake()
                pinStep = "new"
                return
            }
            pinLock.change(pinOld, pinNew)
        }
    }
    // Adding a finger: the PIN first, then touch after touch on the sensor.
    property string fingerResult: ""
    // The next finger to enrol: thumbs first (the sensor is in the power key).
    function nextFinger() {
        const order = ["right-thumb", "left-thumb", "right-index-finger", "left-index-finger",
                       "right-middle-finger", "left-middle-finger", "right-ring-finger",
                       "left-ring-finger", "right-little-finger", "left-little-finger"]
        for (const f of order)
            if (fingerprint.fingers.indexOf(f) < 0)
                return f
        return ""
    }
    Connections {
        target: quick.fingerprint
        function onEnrolled(ok, error) {
            quick.fingerResult = ok ? qsTr("Added. Your finger unlocks the phone now.") : error
            if (ok)
                quick.haptics.click()
        }
    }
    onShownChanged: {
        if (!shown && fingerprint.enrolling)
            fingerprint.stop()
        if (!shown && renamingHotspot) {
            renamingHotspot = false
            keyboard.stopCapture()
        }
    }
    Connections {
        target: quick.pinLock
        function onVerified(ok) {
            if (quick.pinAction !== "finger")
                return
            if (!ok) {
                lockPad.clear()
                lockPad.shake()
                lockPad.messageIsError = true
                lockPad.message = quick.pinLock.wait > 0 ? qsTr("Too many tries: wait %1 s").arg(quick.pinLock.wait)
                                                         : qsTr("Wrong PIN")
                return
            }
            quick.pinStep = ""
            quick.pinAction = ""
            quick.fingerprint.enroll(quick.nextFinger())
        }
        function onChanged(ok, error) {
            if (ok) {
                quick.pinAction = ""
                quick.pinStep = ""
                quick.pinOld = quick.pinNew = ""
                return
            }
            lockPad.messageIsError = true
            lockPad.message = error
            lockPad.shake()
            quick.pinStep = quick.pinAction === "set" ? "new" : "old"
        }
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
    // A short, wide screen (a phone turned sideways): the overview in two columns.
    readonly property bool twoColumns: width > height * 1.4

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

    // System actions, as Omarchy's system menu; the camera and torch on the other side.
    component ActionsRow: Item {
        height: 44
        Row {
            anchors.left: parent.left
            spacing: 10
            RoundButton {
                icon: "󰄀"
                onClicked: {
                    quick.haptics.click()
                    quick.apps.launch("org.mobarchy.Camera")
                    quick.close()
                }
            }
            RoundButton {
                visible: quick.torch.available
                icon: quick.torch.on ? "󰛨" : "󰛩"
                lit: quick.torch.on
                onClicked: { quick.haptics.click(); quick.torch.toggle() }
            }
            RoundButton {
                icon: "󰒓"
                onClicked: quick.page = 6
            }
        }
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

    component RoundButton: Rectangle {
        id: rb
        property string icon
        property string label
        property bool warn: false
        property bool lit: false            // on (the torch)
        signal clicked()
        width: Math.max(52, row.implicitWidth + 32); height: 44
        radius: 22
        color: warn ? Qt.rgba(Theme.red.r, Theme.red.g, Theme.red.b, 0.18)
             : lit ? Theme.fg : Theme.panel
        border.color: warn ? Theme.red : Theme.line
        scale: rbTap.pressed ? 0.94 : 1
        Behavior on scale { NumberAnimation { duration: 90 } }
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Text { text: rb.icon; color: rb.warn ? Theme.red : rb.lit ? Theme.bg : Theme.fg; font { family: Theme.font; pixelSize: 18 } }
            Text {
                visible: rb.label.length > 0
                anchors.verticalCenter: parent.verticalCenter
                text: rb.label
                color: rb.warn ? Theme.red : rb.lit ? Theme.bg : Theme.fg
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
        // as tall as the page, up to the screen (the pages scroll beyond that)
        height: Math.min([overview, wifiPage, bluetoothPage, lockPage, notesPage, hotspotPage, settingsPage][quick.page].implicitHeight + 28 + handle.height,
                         quick.height)
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        y: -(1 - quick.openness) * height
        // (rounded at the bottom only: it comes down from under the status bar)
        radius: 18
        topLeftRadius: 0
        topRightRadius: 0
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
            // One column; two side by side on a short, wide screen (a phone turned sideways):
            // the clock, sliders and media on the left, the tiles and actions on the right.
            Flickable {
                id: overviewFlick
                width: pages.width
                height: pages.height
                contentHeight: overview.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Grid {
                    id: overview
                    x: 20
                    width: pages.width - 40
                    columns: quick.twoColumns ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 18
                    readonly property real columnWidth: quick.twoColumns ? (width - columnSpacing) / 2 : width

                    Column {
                        width: overview.columnWidth
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
                                Row {
                                    anchors.right: parent.right
                                    spacing: 8
                                    BatteryIcon {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 30; height: 15
                                        level: quick.system.battery
                                        charging: quick.system.charging
                                        animate: quick.visible
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: quick.system.battery + "%"
                                        color: quick.system.battery <= 15 && !quick.system.charging ? Theme.red : Theme.fg
                                        font { family: Theme.font; pixelSize: 18 }
                                    }
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
                        // where the sound goes, when there is a choice
                        Flow {
                            width: parent.width
                            visible: quick.volume.available && quick.volume.outputs.length > 1
                            spacing: 8
                            Repeater {
                                model: quick.volume.outputs
                                delegate: Rectangle {
                                    id: outputPill
                                    required property var modelData
                                    readonly property bool current: modelData.name === quick.volume.output
                                    width: Math.min(outputLabel.implicitWidth + 32, parent.width)
                                    height: 40
                                    radius: 20
                                    color: current ? Theme.accent : outputTap.pressed ? Theme.line : Theme.panel
                                    Text {
                                        id: outputLabel
                                        anchors { left: parent.left; right: parent.right; leftMargin: 16; rightMargin: 16
                                                  verticalCenter: parent.verticalCenter }
                                        text: (outputPill.modelData.kind === "bluetooth" ? "󰂯  "
                                               : outputPill.modelData.kind === "headphones" ? "󰋋  " : "󰓃  ")
                                              + outputPill.modelData.description
                                        elide: Text.ElideRight
                                        color: outputPill.current ? Theme.bg : Theme.fg
                                        font { family: Theme.font; pixelSize: 13; bold: outputPill.current }
                                    }
                                    MouseArea {
                                        id: outputTap
                                        anchors.fill: parent
                                        onClicked: quick.volume.setOutput(outputPill.modelData.name)
                                    }
                                }
                            }
                        }

                        NowPlaying {
                            width: parent.width
                            player: quick.media
                        }

                        ActionsRow {
                            width: parent.width
                            visible: quick.twoColumns
                        }
                    }

                    Column {
                        width: overview.columnWidth
                        spacing: 18

                        Grid {
                            id: tiles
                            width: parent.width
                            columns: quick.wide && !quick.twoColumns ? 3 : 2
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
                                detail: quick.radioSettings.flight ? qsTr("On") : qsTr("Off")
                                active: quick.radioSettings.flight
                                supported: quick.rfkill.available || quick.telephony.available
                                onClicked: {
                                    // the mobile radio follows the setting (Telephony.radio);
                                    // Wi-Fi and Bluetooth go down, and come back as they were
                                    const on = !quick.radioSettings.flight
                                    quick.radioSettings.flight = on
                                    if (quick.rfkill.available)
                                        quick.rfkill.flightMode = on
                                }
                            }
                            Tile {
                                icon: quick.pinLock.hasPin ? "󰌾" : "󰍁"
                                label: qsTr("Screen lock")
                                detail: quick.pinLock.hasPin ? qsTr("PIN") : qsTr("Swipe only")
                                active: quick.pinLock.hasPin
                                chevron: true
                                onClicked: quick.showLock()
                            }
                            Tile {
                                visible: quick.rotation.supported
                                icon: quick.rotation.locked ? "󰑙" : "󰑵"
                                label: qsTr("Rotation")
                                detail: quick.rotation.locked ? qsTr("Locked") : qsTr("Automatic")
                                active: !quick.rotation.locked
                                onClicked: quick.rotation.setLocked(!quick.rotation.locked)
                            }
                            Tile {
                                readonly property var t: quick.telephony
                                readonly property string tech: ({ nr: "5G", lte: "4G", umts: "3G", hspa: "3G",
                                    edge: "2G", gprs: "2G", gsm: "2G" })[t.technology] ?? ""
                                icon: "󰒢"
                                label: qsTr("Mobile network")
                                detail: quick.radioSettings.flight ? qsTr("Flight mode")
                                      : !quick.radioSettings.mobile ? qsTr("Off")
                                      : !t.simPresent ? qsTr("No SIM")
                                      : t.pinRequired !== "none" ? qsTr("SIM locked")
                                      : t.registration === "registered" || t.registration === "roaming"
                                        ? (t.operatorName || qsTr("Mobile")) + (tech ? " · " + tech : "")
                                      : t.registration === "searching" || !t.online ? qsTr("Searching …")
                                      : qsTr("No service")
                                active: !quick.radioSettings.flight && quick.radioSettings.mobile
                                        && (t.registration === "registered" || t.registration === "roaming")
                                supported: t.available
                                onClicked: if (!quick.radioSettings.flight)
                                               quick.radioSettings.mobile = !quick.radioSettings.mobile
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

                        // Notifications: a box with how many; a tap shows them (a page of their own)
                        Rectangle {
                            width: parent.width
                            height: 56
                            visible: quick.notifications.count > 0
                            radius: 14
                            color: notesTap.pressed ? Theme.line : Theme.panel
                            Text {
                                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                                text: "󰂚  " + (quick.notifications.count === 1 ? qsTr("1 notification")
                                                                                 : qsTr("%1 notifications").arg(quick.notifications.count))
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 15 }
                            }
                            Text {
                                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                                text: "\uf054"     // nf-fa-chevron_right
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 14 }
                            }
                            MouseArea { id: notesTap; anchors.fill: parent; onClicked: quick.page = 4 }
                        }

                        ActionsRow {
                            width: parent.width
                            visible: !quick.twoColumns
                        }
                    }
                }
            }

            // --- settings: the smaller ones ----------------------------------------------
            Flickable {
                x: 6 * pages.width
                width: pages.width
                height: pages.height
                contentHeight: settingsPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: settingsPage
                    x: 20
                    width: pages.width - 40
                    spacing: 10

                    Item {
                        width: parent.width
                        height: 40
                        MouseArea {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: settingsBackRow.implicitWidth + 16
                            onClicked: quick.page = 0
                            Row {
                                id: settingsBackRow
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                Text { text: "\uf053"; color: Theme.muted;  // nf-fa-chevron_left
                                       font { family: Theme.font; pixelSize: 16 }
                                       anchors.verticalCenter: parent.verticalCenter }
                                Text { text: qsTr("Settings"); color: Theme.fg
                                       font { family: Theme.font; pixelSize: 18; bold: true } }
                            }
                        }
                    }

                    // the screen: how long without a touch before it goes dark
                    Text {
                        text: qsTr("Screen off after")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    Flow {
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [{s: 30, t: qsTr("30 s")}, {s: 60, t: qsTr("1 min")}, {s: 120, t: qsTr("2 min")},
                                    {s: 300, t: qsTr("5 min")}, {s: 600, t: qsTr("10 min")}, {s: 0, t: qsTr("Never")}]
                            delegate: Rectangle {
                                id: choice
                                required property var modelData
                                readonly property bool chosen: quick.screenSettings.timeout === modelData.s
                                width: choiceText.implicitWidth + 32
                                height: 40
                                radius: 20
                                color: chosen ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                     : choiceTap.pressed ? Theme.line : Theme.panel
                                border.color: chosen ? Theme.accent : Theme.line
                                Text {
                                    id: choiceText
                                    anchors.centerIn: parent
                                    text: choice.modelData.t
                                    color: choice.chosen ? Theme.accent : Theme.fg
                                    font { family: Theme.font; pixelSize: 14 }
                                }
                                MouseArea {
                                    id: choiceTap
                                    anchors.fill: parent
                                    onClicked: {
                                        quick.haptics.click()
                                        quick.screenSettings.timeout = choice.modelData.s
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        width: parent.width
                        visible: quick.screenSettings.timeout === 0
                        text: qsTr("The screen stays on until you turn it off with the power key.")
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
            }

            // --- the hotspot ---------------------------------------------------------------
            Flickable {
                x: 5 * pages.width
                width: pages.width
                height: pages.height
                contentHeight: hotspotPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: hotspotPage
                    x: 20
                    width: pages.width - 40
                    spacing: 14

                    Item {
                        width: parent.width
                        height: 40
                        MouseArea {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: hotspotBackRow.implicitWidth + 16
                            onClicked: quick.page = 1
                            Row {
                                id: hotspotBackRow
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                Text { text: "\uf053"; color: Theme.muted;  // nf-fa-chevron_left
                                       font { family: Theme.font; pixelSize: 16 }
                                       anchors.verticalCenter: parent.verticalCenter }
                                Text { text: qsTr("Hotspot"); color: Theme.fg
                                       font { family: Theme.font; pixelSize: 18; bold: true } }
                            }
                        }
                        Switch {
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            checked: quick.hotspot.on
                            enabled: !quick.hotspot.busy
                            onToggled: quick.hotspot.setOn(!quick.hotspot.on)
                        }
                    }
                    Text {
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: quick.hotspot.busy ? qsTr("…")
                            : quick.hotspot.error !== "" ? quick.hotspot.error
                            : quick.hotspot.on ? qsTr("Sharing this phone's internet. Join with the name and password, or scan the code.")
                            : qsTr("Off. Turn it on to share this phone's internet over Wi-Fi.")
                        color: quick.hotspot.error !== "" ? Theme.red : Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    Rectangle {
                        width: parent.width
                        height: hotspotFacts.implicitHeight + 28
                        radius: 14
                        color: Theme.panel
                        Column {
                            id: hotspotFacts
                            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                            spacing: 10
                            component Fact: Row {
                                id: fact
                                property string label
                                property string value
                                spacing: 12
                                Text { width: 90; text: fact.label; color: Theme.muted
                                       font { family: Theme.font; pixelSize: 13 } }
                                Text { text: fact.value; color: Theme.fg
                                       font { family: Theme.font; pixelSize: 15; bold: true } }
                            }
                            // the name: a tap to type a new one (the shell's keyboard, as the
                            // drawer's search: quick settings take no text input); Return keeps it
                            Item {
                                width: parent.width
                                height: nameFact.height
                                Fact {
                                    id: nameFact
                                    label: qsTr("Name")
                                    value: quick.renamingHotspot ? quick.keyboard.captured + "▏"
                                                                 : quick.hotspot.name + "  ✎"
                                }
                                MouseArea {
                                    anchors { fill: parent; margins: -8 }
                                    onClicked: quick.renamingHotspot ? quick.renameHotspot() : quick.startRenamingHotspot()
                                }
                            }
                            Fact { label: qsTr("Password"); value: quick.hotspot.password }
                            Fact { label: qsTr("Joined"); value: quick.hotspot.on ? quick.hotspot.clients : "–" }
                        }
                    }
                    // the code: a camera joins with it
                    Rectangle {
                        visible: quick.hotspot.qr !== ""
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 220; height: 220; radius: 16
                        color: "white"
                        Image {
                            anchors { fill: parent; margins: 10 }
                            source: quick.hotspot.qr ? "file://" + quick.hotspot.qr : ""
                            smooth: false
                            fillMode: Image.PreserveAspectFit
                        }
                    }
                    // a new password (to stop those who had the old one)
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("New password")
                        color: Theme.accent
                        font { family: Theme.font; pixelSize: 13 }
                        bottomPadding: 10
                        MouseArea {
                            anchors { fill: parent; margins: -10 }
                            enabled: !quick.hotspot.busy
                            onClicked: {
                                const part = () => Math.floor(Math.random() * 0x10000).toString(16).padStart(4, "0")
                                quick.hotspot.configure(quick.hotspot.name, part() + "-" + part() + "-" + part())
                            }
                        }
                    }
                }
            }

            // --- notifications --------------------------------------------------------------
            Flickable {
                x: 4 * pages.width
                width: pages.width
                height: pages.height
                contentHeight: notesPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: notesPage
                    x: 20
                    width: pages.width - 40
                    spacing: 10

                    Item {
                        width: parent.width
                        height: 40
                        MouseArea {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: notesBackRow.implicitWidth + 16
                            onClicked: quick.page = 0
                            Row {
                                id: notesBackRow
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 10
                                Text { text: "\uf053"; color: Theme.muted;  // nf-fa-chevron_left
                                       font { family: Theme.font; pixelSize: 16 }
                                       anchors.verticalCenter: parent.verticalCenter }
                                Text { text: qsTr("Notifications"); color: Theme.fg
                                       font { family: Theme.font; pixelSize: 18; bold: true } }
                            }
                        }
                        Text {
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            visible: quick.notifications.count > 0
                            text: qsTr("Clear all")
                            color: Theme.accent
                            font { family: Theme.font; pixelSize: 13 }
                            MouseArea {
                                anchors { fill: parent; margins: -10 }
                                onClicked: {
                                    quick.notifications.clear()
                                    quick.page = 0
                                }
                            }
                        }
                    }
                    Text {
                        visible: quick.notifications.count === 0
                        text: qsTr("No notifications")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    Repeater {
                        model: quick.notifications
                        delegate: NotificationCard {
                            width: notesPage.width
                            onActivated: {
                                quick.notifications.invoke(notificationId, "default")
                                quick.close()
                            }
                            onDismissed: quick.notifications.dismiss(notificationId)
                            onAction: (key) => quick.notifications.invoke(notificationId, key)
                        }
                    }
                }
            }
            // (none left: back to the overview)
            Connections {
                target: quick.notifications
                function onCountChanged() { if (quick.notifications.count === 0 && quick.page === 4) quick.page = 0 }
            }

            // --- Wi-Fi ------------------------------------------------------------------
            Flickable {
                x: pages.width
                width: pages.width
                height: pages.height
                contentHeight: wifiPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: wifiPage
                    x: 20
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
                        maximumFlickVelocity: Theme.flickVelocity
                        flickDeceleration: Theme.flickDeceleration
                        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
                        id: networks
                        visible: quick.rfkill.wifiEnabled
                        width: parent.width
                        height: Math.min(contentHeight, quick.height * 0.55)
                        clip: true
                        spacing: 6
                        currentIndex: -1  // see Home.qml
                        highlightFollowsCurrentItem: false
                        boundsBehavior: Theme.boundsBehavior
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

                    // the hotspot: the phone's internet shared over Wi-Fi (its own page)
                    Rectangle {
                        width: parent.width
                        height: 56
                        visible: quick.hotspot.available
                        radius: 14
                        color: hotspotTap.pressed ? Theme.line : Theme.panel
                        border.color: quick.hotspot.on ? Theme.accent : "transparent"
                        Text {
                            id: hotspotIcon
                            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                            text: "󰀃"
                            color: quick.hotspot.on ? Theme.accent : Theme.fg
                            font { family: Theme.font; pixelSize: 20 }
                        }
                        Column {
                            anchors { left: hotspotIcon.right; leftMargin: 14; verticalCenter: parent.verticalCenter }
                            Text {
                                text: qsTr("Hotspot")
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 14 }
                            }
                            Text {
                                text: !quick.hotspot.on ? qsTr("Off")
                                    : quick.hotspot.clients === 1 ? qsTr("1 device")
                                    : quick.hotspot.clients > 1 ? qsTr("%1 devices").arg(quick.hotspot.clients)
                                    : qsTr("On")
                                color: quick.hotspot.on ? Theme.accent : Theme.muted
                                font { family: Theme.font; pixelSize: 11 }
                            }
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                            text: "\uf054"     // nf-fa-chevron_right
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 14 }
                        }
                        MouseArea { id: hotspotTap; anchors.fill: parent; onClicked: quick.page = 5 }
                    }
                }
            }

            // --- Bluetooth --------------------------------------------------------------
            Flickable {
                x: 2 * pages.width
                width: pages.width
                height: pages.height
                contentHeight: bluetoothPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: bluetoothPage
                    x: 20
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
                        maximumFlickVelocity: Theme.flickVelocity
                        flickDeceleration: Theme.flickDeceleration
                        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
                        id: bluetoothDevices
                        visible: quick.rfkill.bluetoothEnabled && quick.bluetooth.available
                        width: parent.width
                        height: Math.min(contentHeight, quick.height * 0.55)
                        clip: true
                        spacing: 6
                        currentIndex: -1  // see Home.qml
                        highlightFollowsCurrentItem: false
                        boundsBehavior: Theme.boundsBehavior
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

            // --- screen lock ------------------------------------------------------------
            Flickable {
                x: 3 * pages.width
                width: pages.width
                height: pages.height
                contentHeight: lockPage.implicitHeight
                clip: true
                interactive: contentHeight > height
                boundsBehavior: Flickable.StopAtBounds
                maximumFlickVelocity: Theme.flickVelocity
                flickDeceleration: Theme.flickDeceleration

                Column {
                    id: lockPage
                    x: 20
                    width: pages.width - 40
                    spacing: 18

                    MouseArea {
                        width: lockBackRow.implicitWidth + 16
                        height: 40
                        onClicked: {
                            if (quick.fingerprint.enrolling) {
                                quick.fingerprint.stop()
                            } else if (quick.fingerResult !== "") {
                                quick.fingerResult = ""
                            } else if (quick.pinStep !== "") {
                                quick.pinStep = ""
                                quick.pinAction = ""
                            } else {
                                quick.page = 0
                            }
                        }
                        Row {
                            id: lockBackRow
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Text { text: "\uf053"; color: Theme.muted; font { family: Theme.font; pixelSize: 16 }
                                   anchors.verticalCenter: parent.verticalCenter }
                            Text { text: qsTr("Screen lock"); color: Theme.fg
                                   font { family: Theme.font; pixelSize: 18; bold: true } }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: quick.pinStep === "" && !quick.fingerprint.enrolling && quick.fingerResult === ""
                        text: quick.pinLock.hasPin
                              ? qsTr("The phone asks for your PIN after the swipe. It also protects your saved logins (the keyring).")
                              : qsTr("Without a PIN, a swipe unlocks the phone. A PIN also protects your saved logins (the keyring).")
                        wrapMode: Text.Wrap
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    Row {
                        visible: quick.pinStep === "" && !quick.fingerprint.enrolling && quick.fingerResult === ""
                        spacing: 10
                        RoundButton {
                            visible: !quick.pinLock.hasPin
                            icon: "󰌾"
                            label: qsTr("Set a PIN")
                            onClicked: quick.startPin("set")
                        }
                        RoundButton {
                            visible: quick.pinLock.hasPin
                            icon: "󰌾"
                            label: qsTr("Change PIN")
                            onClicked: quick.startPin("change")
                        }
                        RoundButton {
                            visible: quick.pinLock.hasPin
                            icon: "󰍁"
                            label: qsTr("Remove PIN")
                            warn: true
                            onClicked: quick.startPin("remove")
                        }
                    }
                    // --- fingers ---
                    Column {
                        width: parent.width
                        spacing: 10
                        visible: quick.fingerprint.available && quick.pinStep === ""
                                 && !quick.fingerprint.enrolling && quick.fingerResult === ""
                        Text {
                            text: qsTr("Fingerprints"); color: Theme.fg
                            font { family: Theme.font; pixelSize: 15; bold: true }
                        }
                        Text {
                            width: parent.width
                            text: quick.pinLock.hasPin
                                  ? qsTr("A finger on the power key unlocks the phone. After a restart, it takes the PIN.")
                                  : qsTr("Set a PIN first: a finger is a quicker way to give it.")
                            wrapMode: Text.Wrap
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        Repeater {
                            model: quick.fingerprint.fingers
                            delegate: Rectangle {
                                id: fingerRow
                                required property string modelData
                                width: parent.width; height: 48; radius: 12
                                color: Theme.panel
                                Text {
                                    anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
                                    text: "󰈷   " + quick.fingerprint.label(fingerRow.modelData)
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 14 }
                                }
                                Text {
                                    anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                                    text: "󰆴"; color: Theme.red
                                    font { family: Theme.font; pixelSize: 18 }
                                    MouseArea {
                                        anchors { fill: parent; margins: -12 }
                                        onClicked: quick.confirmOr("remove-" + fingerRow.modelData,
                                                                   () => quick.fingerprint.remove(fingerRow.modelData))
                                    }
                                }
                                Text {
                                    visible: quick.confirming === "remove-" + fingerRow.modelData
                                    anchors { right: parent.right; rightMargin: 48; verticalCenter: parent.verticalCenter }
                                    text: qsTr("Tap again to remove"); color: Theme.red
                                    font { family: Theme.font; pixelSize: 12 }
                                }
                            }
                        }
                        RoundButton {
                            visible: quick.pinLock.hasPin && quick.nextFinger() !== ""
                            icon: "󰈷"
                            label: qsTr("Add a fingerprint")
                            onClicked: quick.startPin("finger")
                        }
                    }

                    // Enrolling: a ring that fills, touch by touch.
                    Column {
                        width: parent.width
                        spacing: 16
                        visible: quick.fingerprint.enrolling || quick.fingerResult !== ""
                        Item {
                            width: parent.width; height: 180
                            Canvas {
                                id: ring
                                anchors.centerIn: parent
                                width: 160; height: 160
                                property real progress: quick.fingerprint.enrolling ? quick.fingerprint.progress
                                                      : quick.fingerResult.startsWith(qsTr("Added")) ? 1 : 0
                                Behavior on progress { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }
                                onProgressChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.lineWidth = 8
                                    ctx.lineCap = "round"
                                    ctx.strokeStyle = Theme.line
                                    ctx.beginPath()
                                    ctx.arc(80, 80, 72, 0, 2 * Math.PI)
                                    ctx.stroke()
                                    if (progress > 0) {
                                        ctx.strokeStyle = progress >= 1 ? Theme.green : Theme.accent
                                        ctx.beginPath()
                                        ctx.arc(80, 80, 72, -Math.PI / 2, -Math.PI / 2 + progress * 2 * Math.PI)
                                        ctx.stroke()
                                    }
                                }
                            }
                            Text {
                                anchors.centerIn: ring
                                text: "󰈷"
                                color: ring.progress >= 1 ? Theme.green : Theme.fg
                                font { family: Theme.font; pixelSize: 72 }
                            }
                        }
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.Wrap
                            text: quick.fingerResult !== "" ? quick.fingerResult
                                : quick.fingerprint.hint !== "" ? quick.fingerprint.hint
                                : quick.fingerprint.progress === 0
                                  ? qsTr("Rest your thumb on the power key, without pressing it. Lift, and again.")
                                  : qsTr("Lift, and touch again, a little elsewhere each time (%1 %)")
                                        .arg(Math.round(quick.fingerprint.progress * 100))
                            color: quick.fingerprint.hint !== "" ? Theme.magenta : Theme.fg
                            font { family: Theme.font; pixelSize: 14 }
                        }
                        RoundButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            label: quick.fingerprint.enrolling ? qsTr("Cancel") : qsTr("Done")
                            onClicked: {
                                if (quick.fingerprint.enrolling)
                                    quick.fingerprint.stop()
                                quick.fingerResult = ""
                            }
                        }
                    }

                    PinPad {
                        id: lockPad
                        visible: quick.pinStep !== ""
                        width: parent.width
                        title: quick.pinAction === "finger" ? qsTr("Your PIN, to add a finger")
                             : quick.pinStep === "old" ? qsTr("Current PIN")
                             : quick.pinStep === "new" ? qsTr("New PIN") : qsTr("New PIN again")
                        enabled_: !quick.pinLock.busy && quick.pinLock.wait === 0
                        onEntered: (pin) => quick.pinEntered(pin)
                    }
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
