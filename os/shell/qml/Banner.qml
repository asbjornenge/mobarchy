import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// A new notification, shown for a few seconds at the top. Tap: open it (the app's
// "default" action); swipe up: put it away (it stays in quick settings).
LayerWindow {
    id: banner

    required property Notifications notifications
    // Don't show banners (e.g. while the lock screen is up).
    property bool suppressed: false

    property var current: ({})
    shown: false
    height: card.height + 16
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-banner"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    Connections {
        target: banner.notifications
        function onPosted(id) {
            if (banner.suppressed)
                return
            banner.current = banner.notifications.get(id)
            slide.stop()
            banner.shown = true
            slide.to = 1
            slide.restart()
            hideTimer.interval = banner.current.urgency >= 2 ? 10000 : 5000
            hideTimer.restart()
        }
    }
    function hide() {
        hideTimer.stop()
        slide.to = 0
        slide.restart()
    }

    property real offset: 0  // 0 hidden above, 1 in place
    NumberAnimation {
        id: slide
        target: banner
        property: "offset"
        duration: 220
        easing.type: Easing.OutCubic
        onFinished: if (banner.offset === 0) banner.shown = false
    }
    Timer { id: hideTimer; onTriggered: banner.hide() }

    Rectangle {
        id: card
        x: 12
        width: parent.width - 24
        height: 84
        y: 8 - (1 - banner.offset) * (height + 16)
        radius: 16
        color: Theme.panel
        border.color: banner.current.urgency >= 2 ? Theme.red : Theme.accent

        Image {
            id: icon
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
            width: 36; height: 36
            sourceSize: Qt.size(72, 72)
            asynchronous: true
            source: banner.current.icon ? "image://icon/" + banner.current.icon : ""
            visible: status === Image.Ready
        }
        Column {
            anchors { left: icon.visible ? icon.right : parent.left; leftMargin: 14; right: parent.right
                      rightMargin: 14; verticalCenter: parent.verticalCenter }
            spacing: 2
            Text {
                width: parent.width
                text: banner.current.app || ""
                elide: Text.ElideRight
                color: Theme.muted
                font { family: Theme.font; pixelSize: 11 }
            }
            Text {
                width: parent.width
                text: banner.current.summary || ""
                elide: Text.ElideRight
                color: Theme.fg
                font { family: Theme.font; pixelSize: 14; bold: true }
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: banner.current.body || ""
                elide: Text.ElideRight
                maximumLineCount: 1
                color: Theme.fg
                font { family: Theme.font; pixelSize: 12 }
            }
        }

        EdgeGesture {
            anchors.fill: parent
            direction: -1
            onTapped: {
                banner.notifications.invoke(banner.current.notificationId, "default")
                banner.hide()
            }
            onMoved: (distance) => hideTimer.stop()
            onEnded: (velocity) => banner.hide()
        }
    }
}
