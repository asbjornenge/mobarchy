import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The lock screen: shown when the screen goes off, swiped up to unlock, then the PIN if
// there is one (ADR 0013). It covers the whole screen (overlay layer, ignoring other
// surfaces' exclusive zones). It does not take the keyboard; the PIN has its own pad.
LayerWindow {
    id: lock

    required property SystemStatus system
    required property Notifications notifications
    required property MediaPlayers media
    required property PinLock pinLock
    required property Torch torch
    required property Haptics haptics
    required property Fingerprint fingerprint
    // The screen's insets (the panel under the bezel: Device.inset): the lock screen covers
    // the whole screen, its corner buttons keep clear of them.
    property int insetLeft: 0
    property int insetRight: 0
    property int insetBottom: 0
    // The next alarm (Alarms.nextAlarm), shown under the date; invalid: none
    property date nextAlarm
    signal unlocked()
    signal fingerUnlocked()     // (then unlocked())
    signal touched()  // any interaction: keeps the screen on a little longer

    shown: false
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-lock"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    // The theme's wallpaper (ADR 0025), dimmed so that the clock and notifications stays readable.
    Image {
        anchors.fill: parent
        visible: Theme.wallpaper !== ""
        source: Theme.wallpaper
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        sourceSize.height: 1600
    }
    Rectangle {
        anchors.fill: parent
        visible: Theme.wallpaper !== ""
        color: Theme.bgDark
        opacity: 0.45
    }

    property date now: new Date()
    Timer {
        interval: 1000
        running: lock.visible
        repeat: true
        triggeredOnStart: true
        onTriggered: lock.now = new Date()
    }

    // 0..1: how far the content has been pushed up.
    property real lift: 0
    // Swiped up, and the PIN pad is showing.
    property bool askingPin: false
    // The camera, opened from the lock screen without unlocking (ADR 0018).
    property bool cameraOpen: false
    onShownChanged: {
        cameraOpen = false
        if (shown) { settle.stop(); lift = 0; askingPin = false; pad.clear(); pad.message = "" }
    }

    function swiped() {
        if (pinLock.hasPin) {
            askingPin = true
            lift = 0
        } else {
            unlocked()
        }
    }
    // With a PIN, an enrolled finger on the sensor unlocks too, as long as the lock screen is up;
    // after a restart only once the PIN has been given (it opens the keyring).
    readonly property bool fingerReady: shown && pinLock.hasPin && pinLock.givenSinceBoot
                                        && fingerprint.fingers.length > 0
    onFingerReadyChanged: fingerReady ? fingerprint.verify() : fingerprint.stop()
    Connections {
        target: lock.fingerprint
        function onMatched() {
            if (!lock.fingerReady)
                return
            lock.haptics.click()
            lock.fingerUnlocked()
            lock.unlocked()
        }
        function onNoMatch() {
            if (lock.askingPin) {
                pad.messageIsError = true
                pad.message = qsTr("Not recognised: try again, or the PIN")
            }
            lock.haptics.click()
        }
        // (a verify ends when another one, e.g. enrolling in settings, takes the sensor)
        function onVerifyingChanged() {
            if (lock.fingerReady && !lock.fingerprint.verifying && !lock.fingerprint.enrolling)
                retryVerify.restart()
        }
    }
    Timer { id: retryVerify; interval: 2000; onTriggered: if (lock.fingerReady && !lock.fingerprint.verifying) lock.fingerprint.verify() }
    Connections {
        target: lock.pinLock
        function onVerified(ok) {
            if (!lock.askingPin)
                return          // (the PIN asked elsewhere: settings)
            if (ok) {
                lock.fingerprint.resetLockout()
                lock.unlocked()
                return
            }
            pad.clear()
            pad.shake()
            pad.messageIsError = true
            pad.message = lock.pinLock.wait > 0 ? qsTr("Too many tries: wait %1 s").arg(lock.pinLock.wait)
                                                 : qsTr("Wrong PIN")
        }
        function onWaitChanged() {
            if (lock.askingPin && lock.pinLock.wait > 0)
                pad.message = qsTr("Too many tries: wait %1 s").arg(lock.pinLock.wait)
            else if (lock.askingPin && pad.message.startsWith(qsTr("Too many tries")))
                pad.message = ""
        }
    }

    NumberAnimation {
        id: settle
        target: lock
        property: "lift"
        duration: 220
        easing.type: Easing.OutCubic
        onFinished: if (lock.lift >= 1) lock.swiped()
    }

    Item {
        id: content
        visible: !lock.askingPin
        width: parent.width
        height: parent.height
        y: -lock.lift * lock.height * 0.5
        opacity: 1 - lock.lift

        Column {
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: parent.height * 0.18 }
            spacing: 6
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatTime(lock.now, "HH:mm")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 96; weight: Font.Light }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: lock.now.toLocaleDateString(Qt.locale(), "dddd d MMMM")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 18 }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !isNaN(lock.nextAlarm.getTime())
                // (the weekday too, when it isn't within the next day)
                text: "󰀠 " + (lock.nextAlarm - lock.now < 20 * 3600 * 1000
                              ? Qt.formatTime(lock.nextAlarm, "HH:mm")
                              : lock.nextAlarm.toLocaleString(Qt.locale(), "ddd HH:mm"))
                color: Theme.muted
                font { family: Theme.font; pixelSize: 15 }
            }
            Item { width: 1; height: 18 }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: lock.system.battery >= 0
                spacing: 8
                BatteryIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 26; height: 13
                    level: lock.system.battery
                    charging: lock.system.charging
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: lock.system.battery + "%" + (lock.system.charging ? "  " + qsTr("Charging") : "")
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 15 }
                }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: lock.notifications.count > 0
                text: "󰂚 " + (lock.notifications.count === 1 ? qsTr("1 notification")
                                                              : qsTr("%1 notifications").arg(lock.notifications.count))
                color: Theme.accent
                font { family: Theme.font; pixelSize: 14 }
            }
        }

        Column {
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 56 + lock.insetBottom }
            spacing: 8
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: ""  // nf-fa-chevron_up
                color: Theme.accent
                font { family: Theme.font; pixelSize: 18 }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Swipe up to unlock")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }

    EdgeGesture {
        anchors.fill: parent
        enabled: !lock.askingPin
        onTapped: lock.touched()
        onBegan: {
            lock.touched()
            settle.stop()
        }
        onMoved: (distance) => lock.lift = Math.max(0, Math.min(1, distance / (lock.height * 0.4)))
        onEnded: (velocity) => {
            settle.to = (velocity > 0.5 || lock.lift > 0.5) ? 1 : 0
            settle.restart()
        }
    }

    // What's playing, above the swipe hint; it moves with the content when swiped.
    NowPlaying {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                  leftMargin: 24 + lock.insetLeft; rightMargin: 24 + lock.insetRight
                  bottomMargin: 120 + lock.insetBottom }
        transform: Translate { y: -lock.lift * lock.height * 0.5 }
        opacity: 1 - lock.lift
        compact: true
        visible: !lock.askingPin && player.available && (player.title.length > 0 || player.identity.length > 0)
        player: lock.media
        onVisibleChanged: if (visible) lock.media.refreshPosition()
    }

    // The torch: the other corner.
    Rectangle {
        visible: !lock.askingPin && !lock.cameraOpen && lock.torch.available
        anchors { left: parent.left; bottom: parent.bottom
                  leftMargin: 28 + lock.insetLeft; bottomMargin: 44 + lock.insetBottom }
        width: 56; height: 56; radius: 28
        opacity: 1 - lock.lift
        color: lock.torch.on ? Theme.fg : torchTap.pressed ? Theme.line : Theme.panel
        border.color: Theme.line
        Text {
            anchors.centerIn: parent
            text: lock.torch.on ? "󰛨" : "󰛩"
            color: lock.torch.on ? Theme.bg : Theme.fg
            font { family: Theme.font; pixelSize: 24 }
        }
        TapHandler {
            id: torchTap
            onTapped: { lock.touched(); lock.haptics.click(); lock.torch.toggle() }
        }
    }

    // The camera: a phone's corner shortcut. Locked: its photos are only the ones taken here.
    Rectangle {
        visible: !lock.askingPin && !lock.cameraOpen
        anchors { right: parent.right; bottom: parent.bottom
                  rightMargin: 28 + lock.insetRight; bottomMargin: 44 + lock.insetBottom }
        width: 56; height: 56; radius: 28
        opacity: 1 - lock.lift
        color: cameraTap.pressed ? Theme.line : Theme.panel
        border.color: Theme.line
        Text {
            anchors.centerIn: parent
            text: "󰄀"
            color: Theme.fg
            font { family: Theme.font; pixelSize: 24 }
        }
        TapHandler {
            id: cameraTap
            onTapped: { lock.touched(); lock.haptics.click(); lock.cameraOpen = true }
        }
    }
    Loader {
        id: cameraLoader
        anchors.fill: parent
        active: lock.cameraOpen
        sourceComponent: CameraView { locked: true }
    }
    Connections {
        target: cameraLoader.item
        function onCloseRequested() { lock.cameraOpen = false }
    }
    // the screen stays on while the camera is open
    Timer {
        interval: 10000
        repeat: true
        running: lock.cameraOpen
        onTriggered: lock.touched()
    }

    // The PIN, after the swipe.
    PinPad {
        id: pad
        visible: lock.askingPin
        anchors.centerIn: parent
        title: qsTr("Enter PIN")
        enabled_: !lock.pinLock.busy && lock.pinLock.wait === 0
        onPinChanged: lock.touched()
        onEntered: (pin) => lock.pinLock.verify(pin)
    }
    Text {
        visible: lock.askingPin
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 48 + lock.insetBottom }
        text: qsTr("Cancel")
        color: Theme.muted
        font { family: Theme.font; pixelSize: 14 }
        MouseArea {
            anchors { fill: parent; margins: -16 }
            onClicked: { lock.askingPin = false; pad.clear(); pad.message = "" }
        }
    }
}
