pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQml.Models
import Mobarchy.Shell

// Home: two pages side by side, the notifications (NotificationsPage) and, where home opens,
// the widgets (HomeBoard): the notifications are a swipe right from the widgets. The Frigg card opens the conversation with the agent, which is a layer
// of its own over everything (TalkOverlay, ADR 0037).
Window {
    id: home

    required property Talk talk
    required property MediaPlayers media
    required property Alarms alarms
    required property Telephony telephony
    required property SystemStatus system
    required property AppModel apps
    required property ToplevelModel toplevels
    required property Notifications notifications
    // (dark: nothing on it moves, the orb and the weather stand still)
    property bool screenOn: true
    // the Frigg card tapped
    signal friggAsked()

    // back to the widgets (going home from elsewhere comes to them)
    function showWidgets() { pages.currentIndex = 1; pages.positionViewAtIndex(1, ListView.Beginning) }

    title: "Mobarchy"
    color: Theme.bg
    Component.onCompleted: show()

    // The theme's wallpaper (ADR 0025), behind the widgets dimmed a little.
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
        color: Theme.bg
        opacity: 0.6
    }

    HomeConfig { id: homeConfig }

    ListView {
        id: pages
        anchors.fill: parent
        orientation: ListView.Horizontal
        snapMode: ListView.SnapOneItem
        highlightRangeMode: ListView.StrictlyEnforceRange
        highlightMoveDuration: 250
        boundsBehavior: Flickable.StopAtBounds
        // (not while cards are moved about)
        interactive: !board.editing
        currentIndex: 1
        Component.onCompleted: positionViewAtIndex(1, ListView.Beginning)
        model: ObjectModel {
            NotificationsPage {
                width: pages.width
                height: pages.height
                notifications: home.notifications
                bottomInset: 8
            }
            HomeBoard {
                id: board
                width: pages.width
                height: pages.height
                screenOn: home.screenOn
                config: homeConfig
                media: home.media
                alarms: home.alarms
                telephony: home.telephony
                system: home.system
                apps: home.apps
                toplevels: home.toplevels
                talk: home.talk
                bottomInset: 8
                onFriggAsked: home.friggAsked()
            }
        }
    }

    // which page: two dots at the bottom (the notifications' lit while there are any)
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 10 }
        visible: !board.editing
        spacing: 8
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                width: 6; height: 6; radius: 3
                color: pages.currentIndex === index ? Theme.fg
                     : index === 0 && home.notifications.count > 0 ? Theme.accent : Theme.muted
                opacity: pages.currentIndex === index ? 0.9 : 0.5
            }
        }
    }
}
