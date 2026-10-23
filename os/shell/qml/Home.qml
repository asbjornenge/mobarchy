pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Mobarchy.Shell

// Home: the widgets (HomeBoard). The Frigg card opens the conversation with the agent, which
// is a layer of its own over everything (TalkOverlay, ADR 0037).
Window {
    id: home

    required property Talk talk
    required property MediaPlayers media
    required property Alarms alarms
    required property Telephony telephony
    required property SystemStatus system
    required property AppModel apps
    required property ToplevelModel toplevels
    // (dark: nothing on it moves, the orb and the weather stand still)
    property bool screenOn: true
    // the Frigg card tapped
    signal friggAsked()

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
    }
}
