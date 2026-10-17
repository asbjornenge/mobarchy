pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The apps used last, newest first: a quicker way back than the drawer or the switcher. An
// app counts as used when it leaves the front; the list (app IDs) is kept in home.json
// ("recent"), so it outlives restarts and closed apps stay in it.
BoardCard {
    id: card

    required property ToplevelModel toplevels
    required property AppModel apps
    required property HomeConfig config

    // two rows of four, without a title: the icons say what it is
    readonly property int across: 4
    readonly property int shown: across * 2
    readonly property var recent: (config.config.recent ?? []).filter(id => apps.nameForAppId(id) !== "")

    cols: 4
    wanted: recent.length > 0

    Connections {
        target: card.toplevels
        function onDeactivated(appId) {
            if (!appId)
                return
            const list = [appId].concat((card.config.config.recent ?? []).filter(id => id !== appId))
            card.config.set("recent", list.slice(0, 12))
        }
    }

    Grid {
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        columns: card.across
        rowSpacing: 10
        Repeater {
            model: card.recent.slice(0, card.shown)
            delegate: Item {
                id: app
                required property string modelData
                width: parent.width / card.across
                height: 66
                scale: appTap.pressed ? 0.9 : 1
                Behavior on scale { NumberAnimation { duration: 100 } }
                Image {
                    id: appIcon
                    anchors { horizontalCenter: parent.horizontalCenter; top: parent.top }
                    width: 44; height: 44
                    sourceSize { width: 88; height: 88 }
                    source: "image://icon/" + card.apps.iconForAppId(app.modelData)
                    asynchronous: true
                }
                Text {
                    anchors { top: appIcon.bottom; topMargin: 6; horizontalCenter: parent.horizontalCenter }
                    width: parent.width - 4
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                    text: card.apps.nameForAppId(app.modelData)
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 11 }
                }
                MouseArea {
                    id: appTap
                    anchors.fill: parent
                    onClicked: card.toplevels.activateApp([app.modelData]) || card.apps.launch(app.modelData)
                }
            }
        }
    }
}
