pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Home's second page, a swipe left from the widgets: the notifications, newest first, each a
// card (a tap opens it, ✕ dismisses it), and "Clear all".
Flickable {
    id: page

    required property Notifications notifications
    // room kept free at the bottom
    property real bottomInset: 0

    readonly property real margin: 16
    // (on a wide screen a column of its own, not cards across the whole width)
    readonly property real columnWidth: Math.min(width - 2 * margin, 560)

    contentHeight: column.implicitHeight + bottomInset + 24
    maximumFlickVelocity: Theme.flickVelocity
    flickDeceleration: Theme.flickDeceleration
    boundsBehavior: Theme.boundsBehavior

    Column {
        id: column
        x: (page.width - page.columnWidth) / 2
        width: page.columnWidth
        topPadding: 20
        spacing: 10

        Item {
            width: parent.width
            height: 56
            Text {
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                text: qsTr("Notifications")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 28; bold: true }
            }
            Rectangle {
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                visible: page.notifications.count > 0
                width: clearText.implicitWidth + 32; height: 40; radius: 20
                color: clearTap.pressed ? Theme.line : Theme.panel
                Text {
                    id: clearText
                    anchors.centerIn: parent
                    text: qsTr("Clear all")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 14 }
                }
                MouseArea {
                    id: clearTap
                    anchors { fill: parent; margins: -6 }
                    onClicked: page.notifications.clear()
                }
            }
        }

        Repeater {
            model: page.notifications
            delegate: NotificationCard {
                width: column.width
                onActivated: page.notifications.invoke(notificationId, "default")
                onDismissed: page.notifications.dismiss(notificationId)
                onAction: (key) => page.notifications.invoke(notificationId, key)
            }
        }
    }

    // none: a quiet word in the middle of the page (of the page, not of its content: in a
    // Flickable the parent is the content)
    Column {
        x: (page.width - width) / 2
        y: (page.height - height) / 2
        visible: page.notifications.count === 0
        spacing: 12
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "󰂚"
            color: Theme.muted
            font { family: Theme.font; pixelSize: 40 }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("No notifications")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 15 }
        }
    }
}
