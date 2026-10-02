import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The lock screen: shown when the screen goes off, swiped up to unlock. It covers the
// whole screen (overlay layer, ignoring other surfaces' exclusive zones). It does not take
// the keyboard: a focused layer surface makes squeekboard come up. No PIN or password yet: it keeps pockets from using the phone, it does not
// protect it.
LayerWindow {
    id: lock

    required property SystemStatus system
    required property Notifications notifications
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
    onShownChanged: if (shown) { settle.stop(); lift = 0 }

    NumberAnimation {
        id: settle
        target: lock
        property: "lift"
        duration: 220
        easing.type: Easing.OutCubic
        onFinished: if (lock.lift >= 1) lock.unlocked()
    }

    Item {
        id: content
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
}
