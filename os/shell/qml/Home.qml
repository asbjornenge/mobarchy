pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQml.Models
import Mobarchy.Shell

// Home: three pages side by side, the notifications (NotificationsPage), the widgets
// (HomeBoard), where home opens, and the apps (AppsPage): the notifications a swipe right
// from the widgets, the apps a swipe left. The Frigg card opens the conversation with the agent, which is a layer
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
    required property Keyboard keyboard
    // An app's picture in the middle in place of the widgets (image://windows/<app>/<title>):
    // the app in front, as home's side pages come up over it with ⌘+←/→, so it seems to slide
    // away; and back. "" the widgets. (generation: a new picture of it, Main's)
    property string appPicture: ""
    property int pictureGeneration: 0
    // the middle reached (a slide, a swipe) while it is the app's picture: back to the app
    signal appReached()
    // (dark: nothing on it moves, the orb and the weather stand still)
    property bool screenOn: true
    // the Frigg card tapped
    signal friggAsked()

    // back to the widgets (going home from elsewhere comes to them)
    function showWidgets() { showPage(1, false) }
    // 0 the notifications, 1 the widgets, 2 the apps; moved to (animated) or put there
    readonly property int page: pages.currentIndex
    function showPage(i, animated) {
        pages.currentIndex = i
        if (!animated)
            pages.positionViewAtIndex(i, ListView.Beginning)
    }
    // the apps, searching (laptop mode's Super key)
    function searchApps() {
        pages.currentIndex = 2
        appsPage.startSearch()
    }
    readonly property bool searchingApps: pages.currentIndex === 2 && appsPage.searching

    title: "Mobarchy"
    color: Theme.bg
    // Home left (an app in front, however it got there): the apps' search is over, or the
    // on-screen keyboard went on typing into it and not into the app.
    onActiveChanged: if (!active) appsPage.endSearch()
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
        // (away from the apps: their search is over; on them: the keys are theirs)
        onCurrentIndexChanged: {
            if (currentIndex !== 2)
                appsPage.endSearch()
            else
                appsPage.forceActiveFocus()
            if (currentIndex === 1 && home.appPicture !== "")
                home.appReached()
        }
        model: ObjectModel {
            NotificationsPage {
                width: pages.width
                height: pages.height
                notifications: home.notifications
                bottomInset: 8
            }
            Item {
                width: pages.width
                height: pages.height
                HomeBoard {
                    id: board
                    anchors.fill: parent
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
                    visible: home.appPicture === ""
                }
                Image {
                    anchors.fill: parent
                    visible: home.appPicture !== ""
                    source: home.appPicture !== "" ? home.appPicture + "?" + home.pictureGeneration : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: false
                    cache: false
                }
            }
            AppsPage {
                id: appsPage
                width: pages.width
                height: pages.height
                apps: home.apps
                toplevels: home.toplevels
                keyboard: home.keyboard
                bottomInset: 20
            }
        }
    }

    // which page: dots at the bottom (the notifications' lit while there are any)
    Row {
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 10 }
        visible: !board.editing
        spacing: 8
        Repeater {
            model: 3
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
