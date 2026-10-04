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
    signal unlocked()
    signal touched()  // any interaction: keeps the screen on a little longer

    shown: false
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-lock"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

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
    Connections {
        target: lock.pinLock
        function onVerified(ok) {
            if (ok) {
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
            Item { width: 1; height: 18 }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: lock.system.battery >= 0
                text: (lock.system.charging ? "󰂄 " : "󰁹 ") + lock.system.battery + "%"
                      + (lock.system.charging ? "  " + qsTr("Charging") : "")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
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
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 56 }
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
                  leftMargin: 24; rightMargin: 24; bottomMargin: 120 }
        transform: Translate { y: -lock.lift * lock.height * 0.5 }
        opacity: 1 - lock.lift
        compact: true
        visible: !lock.askingPin && player.available && (player.title.length > 0 || player.identity.length > 0)
        player: lock.media
        onVisibleChanged: if (visible) lock.media.refreshPosition()
    }

    // The camera: a phone's corner shortcut. Locked: its photos are only the ones taken here.
    Rectangle {
        visible: !lock.askingPin && !lock.cameraOpen
        anchors { right: parent.right; bottom: parent.bottom; rightMargin: 28; bottomMargin: 44 }
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
            onTapped: { lock.touched(); lock.cameraOpen = true }
        }
    }
    Loader {
        anchors.fill: parent
        active: lock.cameraOpen
        sourceComponent: CameraView {
            locked: true
            onCloseRequested: lock.cameraOpen = false
        }
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
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 48 }
        text: qsTr("Cancel")
        color: Theme.muted
        font { family: Theme.font; pixelSize: 14 }
        MouseArea {
            anchors { fill: parent; margins: -16 }
            onClicked: { lock.askingPin = false; pad.clear(); pad.message = "" }
        }
    }
}
