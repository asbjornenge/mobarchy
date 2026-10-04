pragma ComponentBehavior: Bound
import QtQuick
import QtCore
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The on-screen keyboard (ADR 0016): an iPhone-like Norwegian layout in Mobarchy's theme.
// Keys show a preview over the finger and type on release; holding a letter offers its
// accented forms; holding space moves the cursor. Several fingers at once work (two thumbs).
// The text goes into the focused app through Keyboard (the input method).
LayerWindow {
    id: kb

    required property Keyboard keyboard
    required property Clipboard clipboard

    shown: keyboard.visible
    color: Theme.bgDark
    LayerShell.Window.scope: "mobarchy-keyboard"
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: height
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    // --- geometry ------------------------------------------------------------------------
    readonly property real pad: 3
    // The keys are at most a phone's width wide, centred: on a wide screen (an unfolded
    // foldable) a full-width row puts the letters too far apart for the thumbs.
    readonly property real maxKeysWidth: 720
    readonly property real keysWidth: Math.min(width, maxKeysWidth)
    readonly property real keysX: (width - keysWidth) / 2
    readonly property real gap: Math.max(5, keysWidth * 0.014)
    readonly property real unit: (keysWidth - 2 * pad - 10 * gap) / 11      // one letter key
    readonly property real keyHeight: Math.min(Math.max(unit * 1.25, 40), 52)
    readonly property real rowHeight: keyHeight + gap * 2
    readonly property real stripHeight: 40      // the top strip; previews of the top row rise into it
    height: stripHeight + 4 * rowHeight + pad

    // --- state ---------------------------------------------------------------------------
    property string mode: "letters"     // letters, numbers, symbols, emoji, clipboard
    property bool shift: false
    property bool caps: false
    readonly property bool upper: caps || shift
    readonly property string purpose: keyboard.purpose
    readonly property bool pad10: purpose === "digits" || purpose === "number" || purpose === "phone"
                                  || purpose === "pin"

    // Shift follows the field: a capital at a sentence start (unless caps lock is on).
    function followField() {
        if (!caps)
            shift = keyboard.capitalize
    }
    Connections {
        target: kb.keyboard
        function onContentChanged() { kb.followField() }
        function onActiveChanged() {
            if (kb.keyboard.active && kb.mode !== "emoji")
                kb.mode = "letters"
            kb.followField()
        }
    }

    // --- layouts -------------------------------------------------------------------------
    // A key: a string types itself; an object is a special key {k: kind, w: width in units}.
    readonly property var letters: [
        ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "å"],
        ["a", "s", "d", "f", "g", "h", "j", "k", "l", "ø", "æ"],
        [{k: "shift", w: 1.45}, {k: "gap", w: 0.25}, "z", "x", "c", "v", "b", "n", "m",
         {k: "gap", w: 0.25}, {k: "back", w: 1.45}],
    ]
    readonly property var numbers: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0"],
        ["-", "/", ":", ";", "(", ")", "kr", "&", "@", "\""],
        [{k: "symbols", w: 1.45}, {k: "gap", w: 0.4}, ".", ",", "?", "!", "'", {k: "gap", w: 0.4},
         {k: "back", w: 1.45}],
    ]
    readonly property var symbols: [
        ["[", "]", "{", "}", "#", "%", "^", "*", "+", "="],
        ["_", "\\", "|", "~", "<", ">", "€", "$", "£", "•"],
        [{k: "numbers", w: 1.45}, {k: "gap", w: 0.4}, ".", ",", "?", "!", "'", {k: "gap", w: 0.4},
         {k: "back", w: 1.45}],
    ]
    readonly property var digitPad: [
        ["1", "2", "3"], ["4", "5", "6"], ["7", "8", "9"],
        [purpose === "phone" ? "+" : (purpose === "number" ? "," : ""), "0", {k: "back", w: 1}],
    ]
    // Held letters offer these (the first is the key itself).
    readonly property var accents: ({
        "a": ["a", "à", "á", "â", "ä", "ã", "ā"], "e": ["e", "è", "é", "ê", "ë", "ē", "ė"],
        "i": ["i", "ì", "í", "î", "ï", "ī"], "o": ["o", "ò", "ó", "ô", "ö", "õ", "œ", "ō"],
        "u": ["u", "ù", "ú", "û", "ü", "ū"], "c": ["c", "ç", "ć", "č"], "n": ["n", "ñ", "ń"],
        "s": ["s", "ß", "ś", "š"], "y": ["y", "ÿ", "ý"], "z": ["z", "ž", "ź", "ż"],
        "l": ["l", "ł"], "d": ["d", "ð"], "å": ["å", "a"], "ø": ["ø", "ö", "o"],
        "æ": ["æ", "ä", "a"], "-": ["-", "–", "—", "·"], "'": ["'", "‘", "’", "´", "`"],
        "\"": ["\"", "«", "»", "„", "“", "”"], "?": ["?", "¿"], "!": ["!", "¡"],
        ".": [".", "…"], "kr": ["kr", "€", "$", "£", "¥"], "0": ["0", "°"],
    })
    readonly property var rows: mode === "numbers" ? numbers : mode === "symbols" ? symbols : letters

    // the bottom row
    function bottomRow() {
        const left = mode === "letters" ? {k: "numbers", w: 1.45} : {k: "letters", w: 1.45}
        const tail = purpose === "email" ? ["@", ".", {k: "return", w: 2.2}]
                   : purpose === "url" ? ["/", ".", {k: "return", w: 2.2}]
                   : [{k: "return", w: 2.4}]
        const space = 11 - 1.45 - 1.1 - tail.reduce((s, k) => s + (typeof k === "string" ? 1 : k.w), 0)
        return [left, {k: "emoji", w: 1.1}, {k: "space", w: space}].concat(tail)
    }

    // --- typing --------------------------------------------------------------------------
    property real lastSpace: 0

    function typeKey(value) {
        let text = value
        if (mode === "letters" && upper && value.length === 1)
            text = value.toUpperCase()
        keyboard.type(text)
        if (shift && !caps)
            shift = false
    }
    function typeSpace() {
        const now = Date.now()
        const before = keyboard.textBefore
        // two spaces quickly after a word: ". "
        if (now - lastSpace < 1200 && before.endsWith(" ") && /[A-Za-z0-9À-ÿæøåÆØÅ]$/.test(before.slice(0, -1))) {
            keyboard.backspace()
            keyboard.type(". ")
            lastSpace = 0
        } else {
            keyboard.type(" ")
            lastSpace = now
        }
        if (mode !== "letters" && mode !== "emoji")
            mode = "letters"
    }
    function act(key) {
        switch (key.k) {
        case "shift":
            if (caps) { caps = false; shift = false }
            else if (shift && Date.now() - shiftTime < 350) caps = true
            else shift = !shift
            shiftTime = Date.now()
            break
        case "back": keyboard.backspace(); break
        case "space": typeSpace(); break
        case "return": keyboard.key("Return"); break
        case "numbers": mode = "numbers"; break
        case "symbols": mode = "symbols"; break
        case "letters": mode = "letters"; followField(); break
        case "emoji": mode = mode === "emoji" ? "letters" : "emoji"; break
        case "hide": keyboard.visible = false; break
        }
    }
    property real shiftTime: 0

    // --- the keys ------------------------------------------------------------------------
    component Key: Rectangle {
        id: key
        required property var spec              // string or {k, w}
        property real rowUnit: kb.unit           // the row's letter width (rows fill the width)
        readonly property bool special: typeof spec !== "string"
        readonly property string kind: special ? spec.k : "char"
        readonly property string value: special ? "" : spec
        property bool down: false
        width: (special ? spec.w : 1) * rowUnit + ((special ? spec.w : 1) - 1) * kb.gap
        height: kb.keyHeight
        radius: 7
        opacity: kind === "gap" ? 0 : 1      // a spacer still takes its room in the row
        // letters lighter than the action keys, as iOS; pressed darker
        color: down ? Theme.muted
             : (special && kind !== "space") ? Theme.panel : Qt.lighter(Theme.panel, 1.3)
        border.color: kind === "shift" && kb.upper ? Theme.accent : "transparent"
        Text {
            anchors.centerIn: parent
            color: key.kind === "shift" && kb.upper ? Theme.accent : Theme.fg
            font { family: Theme.font; pixelSize: key.special ? 16 : Math.min(22, kb.keyHeight * 0.48) }
            text: {
                switch (key.kind) {
                case "char": return kb.mode === "letters" && kb.upper ? key.value.toUpperCase() : key.value
                case "shift": return kb.caps ? "⇪" : "⇧"
                case "back": return "⌫"
                case "space": return qsTr("mellomrom")
                case "return": return qsTr("retur")
                case "numbers": return "123"
                case "symbols": return "#+="
                case "letters": return "ABC"
                case "emoji": return kb.mode === "emoji" ? "ABC" : "☺"
                case "clear": return qsTr("Tøm")
                default: return ""
                }
            }
        }
    }

    // the strip at the top: paste (hold for earlier copies), hide the keyboard. Word
    // suggestions come here later.
    Item {
        id: strip
        width: parent.width; height: kb.stripHeight

        Rectangle {
            id: paste
            visible: kb.clipboard.text !== "" || kb.clipboard.history.length > 0
            anchors { left: parent.left; leftMargin: kb.pad + 2; verticalCenter: parent.verticalCenter }
            height: kb.stripHeight - 10
            width: Math.min(kb.width * 0.7, pasteLabel.implicitWidth + 24)
            radius: height / 2
            color: pasteTap.pressed || kb.mode === "clipboard" ? Theme.line : Theme.panel
            Text {
                id: pasteLabel
                anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                color: Theme.fg
                font { family: Theme.font; pixelSize: 14 }
                // the first line, short
                text: "󰅌  " + (kb.mode === "clipboard" ? qsTr("Utklipp")
                       : kb.clipboard.text !== "" ? kb.clipboard.text.split("\n")[0] : qsTr("Utklipp"))
            }
            TapHandler {
                id: pasteTap
                onTapped: {
                    if (kb.mode === "clipboard") kb.mode = "letters"
                    else if (kb.clipboard.text !== "") kb.keyboard.type(kb.clipboard.text)
                    else kb.mode = "clipboard"
                }
                onLongPressed: kb.mode = "clipboard"
            }
        }
        Text {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            text: "⌄"
            color: Theme.muted
            font { family: Theme.font; pixelSize: 22 }
            TapHandler { onTapped: kb.keyboard.visible = false }
        }
    }

    // the clipboard's history: tap to paste, then back to the letters
    Item {
        id: clipPanel
        visible: kb.mode === "clipboard"
        y: kb.stripHeight
        x: kb.keysX
        width: kb.keysWidth
        height: kb.height - kb.stripHeight
        ListView {
            id: clipList
            anchors { fill: parent; margins: kb.pad; bottomMargin: kb.keyHeight + 2 * kb.gap }
            clip: true
            spacing: kb.gap
            flickDeceleration: Theme.flickDeceleration
            maximumFlickVelocity: Theme.flickVelocity
            boundsBehavior: Flickable.DragAndOvershootBounds
            rebound: Transition {
                NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic }
            }
            model: kb.clipboard.history
            delegate: Rectangle {
                id: entry
                required property string modelData
                width: clipList.width
                height: Math.min(entryText.implicitHeight + 20, 84)
                radius: 8
                color: entryTap.pressed ? Theme.line : Theme.panel
                Text {
                    id: entryText
                    anchors { fill: parent; margins: 10 }
                    text: entry.modelData
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    maximumLineCount: 3
                    font { family: Theme.font; pixelSize: 14 }
                }
                TapHandler {
                    id: entryTap
                    onTapped: {
                        kb.keyboard.type(entry.modelData)
                        kb.clipboard.setText(entry.modelData)
                        kb.mode = "letters"
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: clipList.count === 0
                text: qsTr("Ingenting kopiert ennå")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
        }
        Row {
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right; margins: kb.pad }
            height: kb.keyHeight
            spacing: kb.gap
            Key {
                spec: ({k: "letters", w: 1.6})
                TapHandler { onTapped: kb.act({k: "letters"}) }
            }
            Key {
                spec: ({k: "clear", w: 2.2})
                TapHandler { onTapped: kb.clipboard.clearHistory() }
            }
        }
    }

    // letters, numbers and symbols
    Column {
        id: board
        visible: kb.mode !== "emoji" && kb.mode !== "clipboard" && !kb.pad10
        y: kb.stripHeight
        x: kb.keysX + kb.pad
        width: kb.keysWidth - 2 * kb.pad
        Repeater {
            model: kb.rows.concat([kb.bottomRow()])
            delegate: Item {
                id: row
                required property var modelData
                required property int index
                width: board.width; height: kb.rowHeight
                // the units in this row, so that it spans the width like the letter rows
                readonly property real units: row.modelData.reduce(
                    (s, k) => s + (typeof k === "string" ? 1 : k.w), 0)
                readonly property real rowUnit: (board.width - (row.units - 1) * kb.gap) / row.units
                Row {
                    anchors.centerIn: parent
                    spacing: kb.gap
                    Repeater {
                        model: row.modelData
                        delegate: Key {
                            required property var modelData
                            spec: modelData
                            rowUnit: row.rowUnit
                        }
                    }
                }
            }
        }
    }

    // a number pad (digits, phone numbers, PINs)
    Grid {
        id: numpad
        visible: kb.pad10 && kb.mode !== "emoji" && kb.mode !== "clipboard"
        y: kb.stripHeight + kb.gap
        x: kb.keysX + kb.pad
        columns: 3
        spacing: kb.gap
        readonly property real cell: (kb.keysWidth - 2 * kb.pad - 2 * kb.gap) / 3
        Repeater {
            model: [].concat(...kb.digitPad, [{k: "letters", w: 1}, {k: "space", w: 1}, {k: "return", w: 1}])
            delegate: Key {
                required property var modelData
                spec: modelData
                width: numpad.cell
                height: (kb.height - kb.stripHeight - 2 * kb.gap - 4 * kb.gap) / 5
            }
        }
    }

    // --- touch: every finger on its own ---------------------------------------------------
    // the key under a point (in kb's coordinates), the nearest in its row
    function keyAt(x, y) {
        const area = board.visible ? board : numpad
        let best = null, bestDist = 1e9
        const visit = (item) => {
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (c.spec !== undefined) {
                    if (!c.visible || c.kind === "gap")
                        continue
                    const p = c.mapToItem(kb.contentItem, 0, 0)
                    if (y < p.y - kb.gap || y > p.y + c.height + kb.gap)
                        continue
                    const dx = x < p.x ? p.x - x : x > p.x + c.width ? x - p.x - c.width : 0
                    if (dx < bestDist) { bestDist = dx; best = c }
                } else {
                    visit(c)
                }
            }
        }
        if (area.visible)
            visit(area)
        return best
    }

    property var touches: ({})        // point id -> {key, alt, cursor, x}

    MultiPointTouchArea {
        anchors.fill: parent
        anchors.topMargin: kb.stripHeight
        enabled: kb.mode !== "emoji" && kb.mode !== "clipboard"
        mouseEnabled: true
        onPressed: (points) => {
            for (const p of points) {
                const key = kb.keyAt(p.x, p.y + kb.stripHeight)
                if (!key) continue
                kb.touches[p.pointId] = {key: key, alt: -1, cursor: false, x: p.x}
                key.down = true
                if (key.kind === "char")
                    preview.showFor(key)
                if (key.kind === "back") { kb.keyboard.backspace(); repeat.restart() }
                hold.restart()
                hold.pointId = p.pointId
            }
        }
        onUpdated: (points) => {
            for (const p of points) {
                const t = kb.touches[p.pointId]
                if (!t) continue
                if (t.cursor) {           // space held: the finger moves the cursor
                    const step = 9
                    while (p.x - t.x > step) { kb.keyboard.key("Right"); t.x += step }
                    while (t.x - p.x > step) { kb.keyboard.key("Left"); t.x -= step }
                } else if (alternates.visible && alternates.pointId === p.pointId) {
                    alternates.select(p.x)
                } else if (t.key.kind === "char") {
                    // sliding to another key moves the preview there (as iOS)
                    const key = kb.keyAt(p.x, p.y + kb.stripHeight)
                    if (key && key !== t.key && key.kind === "char") {
                        t.key.down = false
                        t.key = key
                        key.down = true
                        preview.showFor(key)
                        hold.restart()
                        hold.pointId = p.pointId
                    }
                }
            }
        }
        onReleased: (points) => {
            for (const p of points) {
                const t = kb.touches[p.pointId]
                if (!t) continue
                t.key.down = false
                if (alternates.visible && alternates.pointId === p.pointId) {
                    const choice = alternates.chosen()
                    alternates.close()
                    if (choice) kb.typeKey(choice)
                } else if (t.cursor) {
                    // the cursor moved: nothing to type
                } else if (t.key.kind === "char") {
                    kb.typeKey(t.key.value)
                } else if (t.key.kind !== "back") {
                    kb.act(t.key.spec)
                }
                if (t.key.kind === "back") repeat.stop()
                if (hold.pointId === p.pointId) hold.stop()
                delete kb.touches[p.pointId]
            }
            if (Object.keys(kb.touches).length === 0)
                preview.visible = false
        }
        onCanceled: (points) => {
            for (const p of points) {
                const t = kb.touches[p.pointId]
                if (t) t.key.down = false
                delete kb.touches[p.pointId]
            }
            repeat.stop(); hold.stop(); alternates.close(); preview.visible = false
        }
    }

    // held: accents for letters, cursor moving for space
    Timer {
        id: hold
        property int pointId: -1
        interval: 420
        onTriggered: {
            const t = kb.touches[pointId]
            if (!t) return
            if (t.key.kind === "space") {
                t.cursor = true
            } else if (t.key.kind === "char") {
                const options = kb.accents[t.key.value]
                if (options) {
                    preview.visible = false
                    alternates.open(t.key, options, pointId)
                }
            }
        }
    }
    // held backspace repeats, then faster
    Timer {
        id: repeat
        property int count: 0
        interval: 450
        repeat: true
        onTriggered: {
            kb.keyboard.backspace()
            count++
            interval = count > 12 ? 40 : 90
        }
        onRunningChanged: if (!running) { count = 0; interval = 450 }
    }

    // --- the preview over the finger -----------------------------------------------------
    Rectangle {
        id: preview
        visible: false
        z: 10
        radius: 9
        color: Theme.panel
        border.color: Theme.line
        width: label.implicitWidth + 28
        height: kb.keyHeight * 1.15
        property string text: ""
        function showFor(key) {
            text = kb.mode === "letters" && kb.upper ? key.value.toUpperCase() : key.value
            const p = key.mapToItem(kb.contentItem, 0, 0)
            x = Math.max(2, Math.min(kb.width - width - 2, p.x + key.width / 2 - width / 2))
            y = Math.max(0, p.y - height - 4)
            visible = true
        }
        Text {
            id: label
            anchors.centerIn: parent
            text: preview.text
            color: Theme.fg
            font { family: Theme.font; pixelSize: 30 }
        }
    }

    // --- accents (a held letter) ---------------------------------------------------------
    Rectangle {
        id: alternates
        visible: false
        z: 11
        radius: 9
        color: Theme.panel
        border.color: Theme.accent
        height: kb.keyHeight * 1.15
        width: options.length * cellWidth + 8
        property var options: []
        property int current: 0
        property int pointId: -1
        readonly property real cellWidth: Math.max(kb.unit, 34)
        function open(key, list, point) {
            options = list
            current = 0
            pointId = point
            const p = key.mapToItem(kb.contentItem, 0, 0)
            x = Math.max(2, Math.min(kb.width - width - 2, p.x + key.width / 2 - cellWidth / 2 - 4))
            y = Math.max(0, p.y - height - 4)
            visible = true
        }
        function select(fingerX) {
            current = Math.max(0, Math.min(options.length - 1, Math.floor((fingerX - x - 4) / cellWidth)))
        }
        function chosen() { return options[current] || "" }
        function close() { visible = false; pointId = -1 }
        Row {
            x: 4
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: alternates.options
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: alternates.cellWidth; height: alternates.height - 8
                    radius: 6
                    color: index === alternates.current ? Theme.accent : "transparent"
                    Text {
                        anchors.centerIn: parent
                        text: kb.upper && kb.mode === "letters" ? parent.modelData.toUpperCase() : parent.modelData
                        color: parent.index === alternates.current ? Theme.bgDark : Theme.fg
                        font { family: Theme.font; pixelSize: 22 }
                    }
                }
            }
        }
    }

    // --- emoji ---------------------------------------------------------------------------
    Settings {
        id: emojiSettings
        location: StandardPaths.writableLocation(StandardPaths.GenericStateLocation) + "/mobarchy/keyboard.ini"
        category: "emoji"
        property var recent: []
    }
    readonly property var emojiGroups: keyboard.emojiGroups()
    readonly property var groupIcons: ["😀", "👋", "🐶", "🍎", "🚗", "⚽", "💡", "❤️", "🏳️"]
    function typeEmoji(e) {
        keyboard.type(e)
        const recent = [e].concat(emojiSettings.recent.filter(r => r !== e)).slice(0, 32)
        emojiSettings.recent = recent
    }

    Item {
        id: emojiPanel
        visible: kb.mode === "emoji"
        y: kb.stripHeight
        x: kb.keysX
        width: kb.keysWidth
        height: kb.height - kb.stripHeight
        readonly property real cell: Math.floor(width / 8)

        ListView {
            id: emojiList
            anchors { top: parent.top; left: parent.left; right: parent.right; bottom: emojiBar.top }
            orientation: ListView.Horizontal
            clip: true
            flickDeceleration: Theme.flickDeceleration
            maximumFlickVelocity: Theme.flickVelocity
            boundsBehavior: Flickable.DragAndOvershootBounds
            // the recently used first, then Unicode's groups
            model: (emojiSettings.recent.length ? [{name: "recent", emoji: emojiSettings.recent}] : [])
                   .concat(kb.emojiGroups)
            delegate: Grid {
                id: group
                required property var modelData
                flow: Grid.TopToBottom
                rows: Math.max(1, Math.floor(emojiList.height / emojiPanel.cell))
                Repeater {
                    model: group.modelData.emoji
                    delegate: Text {
                        required property string modelData
                        width: emojiPanel.cell; height: emojiPanel.cell
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        text: modelData
                        font { family: "Noto Color Emoji"; pixelSize: emojiPanel.cell * 0.62 }
                        TapHandler { onTapped: kb.typeEmoji(parent.modelData) }
                    }
                }
            }
        }

        Row {
            id: emojiBar
            anchors { bottom: parent.bottom; left: parent.left; right: parent.right; margins: kb.pad }
            height: kb.keyHeight
            spacing: 2
            Key { spec: ({k: "letters", w: 1.3}); TapHandler { onTapped: kb.act({k: "letters"}) } }
            Repeater {
                model: kb.groupIcons
                delegate: Text {
                    required property string modelData
                    required property int index
                    width: (emojiBar.width - 2 * (1.3 * kb.unit + 0.3 * kb.gap)) / kb.groupIcons.length
                           - emojiBar.spacing
                    height: kb.keyHeight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: modelData
                    opacity: 0.8
                    font { family: "Noto Color Emoji"; pixelSize: 18 }
                    TapHandler {
                        onTapped: emojiList.positionViewAtIndex(
                            parent.index + (emojiSettings.recent.length ? 1 : 0), ListView.Beginning)
                    }
                }
            }
            Key {
                spec: ({k: "back", w: 1.3})
                TapHandler { onTapped: kb.keyboard.backspace() }
            }
        }
    }
}
