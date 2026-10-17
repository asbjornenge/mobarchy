import QtQuick
import Mobarchy.Shell

// The home board: widgets in a grid of square cells (4 across on a phone's
// screen, 8 on a wide one), each card 2 or 4 cells wide and high. Cards are placed in order,
// each in the first gap it fits; a card with nothing to show is left out and the rest close up.
Flickable {
    id: board

    required property HomeConfig config
    required property MediaPlayers media
    required property Alarms alarms
    required property Telephony telephony
    required property SystemStatus system
    required property AppModel apps
    required property ToplevelModel toplevels
    // room kept free at the bottom (the Frigg pill)
    property real bottomInset: 0

    readonly property real margin: 16
    readonly property real gap: 12
    readonly property int columns: width >= 640 ? 8 : 4
    readonly property real cell: (width - 2 * margin - (columns - 1) * gap) / columns

    contentHeight: header.height + grid.height + bottomInset + 24
    maximumFlickVelocity: Theme.flickVelocity
    flickDeceleration: Theme.flickDeceleration
    boundsBehavior: Theme.boundsBehavior
    rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }

    // (no clock of its own: the status bar has the time)
    Item { id: header; width: 1; height: 20 }

    // --- the widgets -----------------------------------------------------------------------
    Item {
        id: grid
        x: board.margin
        y: header.height
        width: board.width - 2 * board.margin

        function layout() {
            const taken = []     // taken[row][col]
            const free = (r, c, w, h) => {
                if (c + w > board.columns)
                    return false
                for (let i = r; i < r + h; i++)
                    for (let j = c; j < c + w; j++)
                        if (taken[i] && taken[i][j])
                            return false
                return true
            }
            let rows = 0
            for (let k = 0; k < children.length; k++) {
                const card = children[k]
                if (!card.wanted) {
                    card.visible = false
                    continue
                }
                const w = Math.min(card.cols, board.columns), h = card.rows
                let r = 0, c = 0
                for (;; r++) {
                    c = [...Array(board.columns).keys()].find(j => free(r, j, w, h))
                    if (c !== undefined)
                        break
                }
                for (let i = r; i < r + h; i++) {
                    taken[i] = taken[i] || []
                    for (let j = c; j < c + w; j++)
                        taken[i][j] = true
                }
                card.x = c * (board.cell + board.gap)
                card.y = r * (board.cell + board.gap)
                card.width = w * board.cell + (w - 1) * board.gap
                card.height = h * board.cell + (h - 1) * board.gap
                card.visible = true
                rows = Math.max(rows, r + h)
            }
            height = rows > 0 ? rows * board.cell + (rows - 1) * board.gap : 0
        }
        onWidthChanged: Qt.callLater(layout)
        Component.onCompleted: Qt.callLater(layout)

        WeatherCard { config: board.config; onWantedChanged: Qt.callLater(grid.layout) }
        NextAlarmCard { alarms: board.alarms; apps: board.apps; onWantedChanged: Qt.callLater(grid.layout) }
        MediaCard { player: board.media; apps: board.apps; onWantedChanged: Qt.callLater(grid.layout) }
        RecentAppsCard { toplevels: board.toplevels; apps: board.apps; config: board.config; onWantedChanged: Qt.callLater(grid.layout) }
        MessagesCard { telephony: board.telephony; apps: board.apps; onWantedChanged: Qt.callLater(grid.layout) }
        BatteryCard { system: board.system; onWantedChanged: Qt.callLater(grid.layout) }
    }
}
