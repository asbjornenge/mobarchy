pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// The home board: widgets in a grid of square cells (4 across on a phone's screen, 8 on a
// wide one), each card 2 or 4 cells wide and 1, 2 or 4 high. Cards are placed in order, each in the
// first gap it fits; a card with nothing to show is left out and the rest close up.
//
// Which cards, in what order and size: home.json's "board", [{kind, cols, rows}] (Mobarchy's
// choice until the owner changes it). Holding a card edits the board: drag to move, resize,
// remove, and add back from the cards not on it.
Flickable {
    id: board

    required property HomeConfig config
    required property MediaPlayers media
    required property Alarms alarms
    required property Telephony telephony
    required property SystemStatus system
    required property AppModel apps
    required property ToplevelModel toplevels
    required property Talk talk
    // room kept free at the bottom
    property real bottomInset: 0
    // the Frigg card tapped: the conversation, live
    signal friggAsked()

    property bool editing: false
    property bool adding: false     // the list of cards to add, while editing

    readonly property real margin: 16
    readonly property real gap: 12
    readonly property int columns: width >= 640 ? 8 : 4
    readonly property real cell: (width - 2 * margin - (columns - 1) * gap) / columns

    contentHeight: header.height + grid.height + bottomInset + 24
    interactive: !anyDragged
    maximumFlickVelocity: Theme.flickVelocity
    flickDeceleration: Theme.flickDeceleration
    boundsBehavior: Theme.boundsBehavior
    rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }

    readonly property var defaults: [
        { kind: "frigg", cols: 4, rows: 2 },
        { kind: "weather", cols: 2, rows: 1 }, { kind: "alarm", cols: 2, rows: 1 },
        { kind: "calendar", cols: 4, rows: 2 },
        { kind: "media", cols: 4, rows: 2 }, { kind: "recent", cols: 4, rows: 1 },
        { kind: "messages", cols: 4, rows: 2 }, { kind: "battery", cols: 2, rows: 1 }]
    // the board as the owner has it: known kinds only
    // (a list from home.json is a sequence, not an Array: Array.from)
    readonly property var entries: (config.config.board ? Array.from(config.config.board) : defaults)
                                   .filter(e => cards().some(c => c.kind === e.kind))
    onEntriesChanged: Qt.callLater(grid.layout)
    onEditingChanged: { adding = false; Qt.callLater(grid.layout) }

    function cards() {
        const out = []
        for (let k = 0; k < grid.children.length; k++)
            if (grid.children[k].kind !== undefined)
                out.push(grid.children[k])
        return out
    }
    function card(kind) { return cards().find(c => c.kind === kind) }
    readonly property bool anyDragged: cards().some(c => c.dragged)
    function save(list) { config.set("board", list.map(e => ({ kind: e.kind, cols: e.cols, rows: e.rows }))) }

    function remove(kind) { save(entries.filter(e => e.kind !== kind)) }
    function add(kind) {
        const c = card(kind)
        save(entries.concat([{ kind: kind, cols: c.sizes[0][0], rows: c.sizes[0][1] }]))
        adding = false
    }
    function resize(kind) {
        const c = card(kind)
        save(entries.map(e => {
            if (e.kind !== kind)
                return e
            const i = c.sizes.findIndex(s => s[0] === e.cols && s[1] === e.rows)
            const next = c.sizes[(i + 1) % c.sizes.length]
            return { kind: kind, cols: next[0], rows: next[1] }
        }))
    }
    // a card let go: it goes where its middle is, before the first card whose middle is
    // further on (reading order)
    function drop(kind) {
        const c = card(kind)
        const cx = c.x + c.width / 2, cy = c.y + c.height / 2
        const rest = entries.filter(e => e.kind !== kind)
        let at = rest.findIndex(e => {
            const o = card(e.kind)
            const ox = o.x + o.width / 2, oy = o.y + o.height / 2
            return cy < o.y || (cy < o.y + o.height && cx < ox)
        })
        if (at < 0)
            at = rest.length
        rest.splice(at, 0, entries.find(e => e.kind === kind))
        save(rest)
        Qt.callLater(grid.layout)   // (also when nothing moved: back to its place)
    }

    // --- the editing bar (in the place of a margin while not editing) ----------------------
    Item {
        id: header
        width: board.width
        height: board.editing ? 76 : 20
        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

        Row {
            anchors { left: parent.left; leftMargin: board.margin; verticalCenter: parent.verticalCenter }
            visible: board.editing
            spacing: 10
            component Pill: Rectangle {
                id: pill
                property string label
                property bool primary: false
                signal clicked()
                width: pillText.implicitWidth + 36
                height: 42
                radius: 21
                color: pillTap.pressed ? Theme.line : primary ? Theme.accent : Theme.panel
                border.color: Theme.line
                Text {
                    id: pillText
                    anchors.centerIn: parent
                    text: pill.label
                    color: pill.primary ? Theme.bg : Theme.fg
                    font { family: Theme.font; pixelSize: 14; bold: pill.primary }
                }
                MouseArea { id: pillTap; anchors.fill: parent; onClicked: pill.clicked() }
            }
            Pill {
                label: "󰐕  " + qsTr("Add")
                visible: board.cards().some(c => !board.entries.some(e => e.kind === c.kind))
                onClicked: board.adding = !board.adding
            }
            Pill { label: qsTr("Reset"); onClicked: board.config.set("board", null) }
        }
        Rectangle {
            visible: board.editing
            anchors { right: parent.right; rightMargin: board.margin; verticalCenter: parent.verticalCenter }
            width: doneText.implicitWidth + 36
            height: 42
            radius: 21
            color: doneTap.pressed ? Theme.line : Theme.accent
            Text {
                id: doneText
                anchors.centerIn: parent
                text: qsTr("Done")
                color: Theme.bg
                font { family: Theme.font; pixelSize: 14; bold: true }
            }
            MouseArea { id: doneTap; anchors.fill: parent; onClicked: board.editing = false }
        }
    }

    // the cards not on the board, to add: over the board, dimmed
    Rectangle {
        visible: addList.visible
        y: header.height
        z: 29
        width: board.width
        height: Math.max(board.height, grid.height + board.bottomInset)
        color: Qt.alpha(Theme.bg, 0.92)
        MouseArea { anchors.fill: parent; onClicked: board.adding = false }
    }
    Column {
        id: addList
        visible: board.adding && board.editing
        x: board.margin
        y: header.height
        z: 30
        width: board.width - 2 * board.margin
        spacing: 8
        Repeater {
            model: board.adding ? board.cards().filter(c => !board.entries.some(e => e.kind === c.kind)) : []
            delegate: Rectangle {
                id: choice
                required property var modelData
                width: addList.width
                height: 52
                radius: 16
                color: choiceTap.pressed ? Theme.line : Theme.panel
                border.color: Theme.line
                Text {
                    anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                    text: choice.modelData.name
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 15 }
                }
                Text {
                    anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                    text: "󰐕"
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 18 }
                }
                MouseArea { id: choiceTap; anchors.fill: parent; onClicked: board.add(choice.modelData.kind) }
            }
        }
    }

    // --- the widgets -----------------------------------------------------------------------
    Item {
        id: grid
        x: board.margin
        y: header.height
        width: board.width - 2 * board.margin

        function layout() {
            if (board.anyDragged)
                return
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
            const on = new Set()
            for (const e of board.entries) {
                const card = board.card(e.kind)
                on.add(card)
                // editing, the cards with nothing to show now are there too: to be placed
                if (!card.wanted && !board.editing) {
                    card.visible = false
                    continue
                }
                const w = Math.min(e.cols, board.columns), h = e.rows
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
                card.cols = e.cols
                card.rows = e.rows
                card.x = c * (board.cell + board.gap)
                card.y = r * (board.cell + board.gap)
                card.width = w * board.cell + (w - 1) * board.gap
                card.height = h * board.cell + (h - 1) * board.gap
                card.visible = true
                card.placed = true
                rows = Math.max(rows, r + h)
            }
            for (const card of board.cards())
                if (!on.has(card))
                    card.visible = false
            height = rows > 0 ? rows * board.cell + (rows - 1) * board.gap : 0
        }
        onWidthChanged: Qt.callLater(layout)
        Component.onCompleted: Qt.callLater(layout)

        // each card: its kind, and the board's editing
        component Placed: QtObject {
            required property BoardCard card
            Component.onCompleted: {
                card.openApp = (id) => {
                    if (!board.toplevels.activateApp(board.apps.windowIds(id)))
                        board.apps.launch(id)
                }
                card.editing = Qt.binding(() => board.editing)
                card.live = Qt.binding(() => board.visible && board.Window.active && !board.editing)
                card.wantedChanged.connect(() => Qt.callLater(grid.layout))
                card.held.connect(() => board.editing = true)
                card.removeAsked.connect(() => board.remove(card.kind))
                card.resizeAsked.connect(() => board.resize(card.kind))
                card.dropped.connect(() => board.drop(card.kind))
            }
        }

        FriggCard {
            id: frigg
            kind: "frigg"; name: "Frigg"; sizes: [[4, 2], [2, 2], [4, 4]]
            talk: board.talk
            onAsked: board.friggAsked()
            Placed { card: frigg }
        }
        WeatherCard {
            id: weather
            kind: "weather"; name: qsTr("Weather"); sizes: [[2, 1], [2, 2], [4, 2]]
            config: board.config
            Placed { card: weather }
        }
        NextAlarmCard {
            id: alarm
            kind: "alarm"; name: qsTr("Alarm and timer"); sizes: [[2, 1], [2, 2]]
            alarms: board.alarms; apps: board.apps
            Placed { card: alarm }
        }
        CalendarCard {
            id: calendar
            kind: "calendar"; name: qsTr("Calendar"); sizes: [[4, 2], [2, 1], [2, 2], [4, 4]]
            apps: board.apps
            Placed { card: calendar }
        }
        MediaCard {
            id: media
            kind: "media"; name: qsTr("Now playing"); sizes: [[4, 2]]
            player: board.media; apps: board.apps
            Placed { card: media }
        }
        RecentAppsCard {
            id: recent
            kind: "recent"; name: qsTr("Recent apps"); sizes: [[4, 1], [4, 2]]
            toplevels: board.toplevels; apps: board.apps; config: board.config
            Placed { card: recent }
        }
        MessagesCard {
            id: messages
            kind: "messages"; name: qsTr("Messages and calls"); sizes: [[4, 2], [4, 4]]
            telephony: board.telephony; apps: board.apps
            Placed { card: messages }
        }
        BatteryCard {
            id: battery
            kind: "battery"; name: qsTr("Battery"); sizes: [[2, 1], [2, 2]]
            system: board.system
            Placed { card: battery }
        }
    }
}
