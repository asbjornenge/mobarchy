pragma ComponentBehavior: Bound
import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Laptop mode's keyboard: a foldable half open and turned, its lower half a keyboard (the
// shell's laptop posture, Main.qml). A 75 % Nordic (Norwegian ISO) layout: the function
// row, the big Enter, Ctrl, Alt, Super and AltGr, arrows, Home/End/PgUp/PgDn and Delete.
// It is always up in laptop mode (it can't be hidden), slides in and out, and takes the
// lower half from the apps (an exclusive zone). Modifiers latch with a tap (two taps lock
// them) and hold while a finger is on them. Text goes through Keyboard as the on-screen
// keyboard's does; keys with Ctrl, Alt or Super, and the keys that aren't text, are key
// presses (Keyboard.press). Super alone (pressed and let go) opens the app drawer; with
// another key it is held down.
LayerWindow {
    id: kb

    required property Keyboard keyboard
    // laptop mode: the keyboard is wanted
    property bool wanted: false
    // the height it takes: the lower half of the screen, less the edge strip under it
    property real wantedHeight: 300
    property real sideInsets: 0
    signal superTapped()

    shown: wanted || slide.running
    color: "transparent"
    height: wantedHeight
    LayerShell.Window.scope: "mobarchy-laptop-keyboard"
    LayerShell.Window.layer: LayerShell.Window.LayerTop
    LayerShell.Window.anchors: LayerShell.Window.AnchorBottom | LayerShell.Window.AnchorLeft
                               | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: wanted ? height : 0
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    // --- sliding in and out --------------------------------------------------------------
    property real offset: 1          // 0: in place, 1: below the screen
    onWantedChanged: {
        slide.from = offset
        slide.to = wanted ? 0 : 1
        slide.restart()
        if (!wanted) clearMods(true)
    }
    NumberAnimation {
        id: slide
        target: kb; property: "offset"
        duration: 320; easing.type: Easing.OutCubic
    }

    // --- the layout ----------------------------------------------------------------------
    // A key: x and w in key units (16 across), r its row (0: the function row). Characters:
    // v typed, s with Shift, a with AltGr; letters (l) follow Caps Lock. Other keys: n is
    // the xkb key pressed (with t its label), or k says what it is.
    function ch(x, v, s, a, w) { return {x: x, w: w || 1, k: "char", v: v, s: s, a: a || ""} }
    function lt(x, v, a) { return {x: x, w: 1, k: "char", v: v, s: v.toUpperCase(), a: a || "", l: true} }
    function nk(x, n, t, w) { return {x: x, w: w || 1, k: "named", n: n, t: t} }
    function md(x, m, t, w) { return {x: x, w: w, k: "mod", m: m, t: t} }
    readonly property var rows: [
        [nk(0, "Escape", "esc"),
         nk(1.25, "F1", "F1"), nk(2.25, "F2", "F2"), nk(3.25, "F3", "F3"), nk(4.25, "F4", "F4"),
         nk(5.5, "F5", "F5"), nk(6.5, "F6", "F6"), nk(7.5, "F7", "F7"), nk(8.5, "F8", "F8"),
         nk(9.75, "F9", "F9"), nk(10.75, "F10", "F10"), nk(11.75, "F11", "F11"), nk(12.75, "F12", "F12"),
         nk(14, "Insert", "ins"), nk(15, "Delete", "del")],
        [ch(0, "|", "§"), ch(1, "1", "!"), ch(2, "2", "\"", "@"), ch(3, "3", "#", "£"), ch(4, "4", "¤", "$"),
         ch(5, "5", "%", "€"), ch(6, "6", "&"), ch(7, "7", "/", "{"), ch(8, "8", "(", "["),
         ch(9, "9", ")", "]"), ch(10, "0", "=", "}"), ch(11, "+", "?"), ch(12, "\\", "`", "´"),
         {x: 13, w: 2, k: "back"}, nk(15, "Home", "home")],
        [{x: 0, w: 1.5, k: "named", n: "Tab", t: "⇥"},
         lt(1.5, "q"), lt(2.5, "w"), lt(3.5, "e", "€"), lt(4.5, "r"), lt(5.5, "t"), lt(6.5, "y"),
         lt(7.5, "u"), lt(8.5, "i"), lt(9.5, "o"), lt(10.5, "p"), lt(11.5, "å"), ch(12.5, "¨", "^", "~"),
         {x: 13.75, w: 1.25, k: "enter"}, nk(15, "Prior", "pg↑")],
        [{x: 0, w: 1.75, k: "caps"},
         lt(1.75, "a"), lt(2.75, "s"), lt(3.75, "d"), lt(4.75, "f"), lt(5.75, "g"), lt(6.75, "h"),
         lt(7.75, "j"), lt(8.75, "k"), lt(9.75, "l"), lt(10.75, "ø"), lt(11.75, "æ"), ch(12.75, "'", "*"),
         {x: 13.75, w: 1.25, k: "enter"}, nk(15, "Next", "pg↓")],
        [md(0, "shift", "⇧", 1.25), ch(1.25, "<", ">", "|"),
         lt(2.25, "z"), lt(3.25, "x"), lt(4.25, "c"), lt(5.25, "v"), lt(6.25, "b"), lt(7.25, "n"),
         lt(8.25, "m", "µ"), ch(9.25, ",", ";"), ch(10.25, ".", ":"), ch(11.25, "-", "_"),
         md(12.25, "shift", "⇧", 1.75), nk(14, "Up", "↑"), nk(15, "End", "end")],
        [md(0, "ctrl", "ctrl", 1.25), md(1.25, "super", "󰣇", 1.25), md(2.5, "alt", "alt", 1.25),
         {x: 3.75, w: 6.25, k: "space"}, md(10, "altgr", "alt gr", 1.25), md(11.25, "ctrl", "ctrl", 1.25),
         nk(13, "Left", "←"), nk(14, "Down", "↓"), nk(15, "Right", "→")],
    ]

    // --- geometry ------------------------------------------------------------------------
    readonly property real pad: 8
    readonly property real gap: 6
    readonly property real fnScale: 0.72      // the function row is lower
    readonly property real keysWidth: width - sideInsets - 2 * pad
    readonly property real unitW: (keysWidth + gap) / 16
    readonly property real unitH: (height - 2 * pad + gap) / (5 + fnScale)
    function rowY(r) { return pad + (r === 0 ? 0 : (fnScale + r - 1) * unitH) }
    function rowH(r) { return (r === 0 ? fnScale : 1) * unitH - gap }

    // --- modifiers -----------------------------------------------------------------------
    property var latched: ({})          // mod -> true (until the next key)
    property var locked: ({})           // mod -> true (until tapped again)
    property var held: ({})             // mod -> fingers on it
    property bool caps: false
    property var lastModTap: ({})
    function modOn(m) { return !!(latched[m] || locked[m] || held[m]) }
    readonly property bool shiftOn: modOn("shift")
    function modMask() {
        return (modOn("ctrl") ? 1 : 0) | (modOn("alt") ? 2 : 0) | (modOn("super") ? 4 : 0)
             | (modOn("shift") ? 8 : 0) | (modOn("altgr") ? 16 : 0)
    }
    function setMod(map, m, on) {
        const n = Object.assign({}, kb[map])
        if (on) n[m] = true
        else delete n[m]
        kb[map] = n
    }
    function tapMod(m) {
        const now = Date.now()
        if (locked[m]) {
            setMod("locked", m, false)
        } else if (latched[m] && now - (lastModTap[m] || 0) < 350) {
            setMod("latched", m, false)
            setMod("locked", m, true)
        } else {
            setMod("latched", m, !latched[m])
        }
        const t = Object.assign({}, lastModTap)
        t[m] = now
        lastModTap = t
    }
    // after a key: the latched ones are used up (the held and locked stay)
    property var usedWhileHeld: ({})
    function keyTyped() {
        latched = ({})
        const u = Object.assign({}, usedWhileHeld)
        for (const m in held) u[m] = true
        usedWhileHeld = u
    }
    function clearMods(all) {
        latched = ({})
        held = ({})
        if (all) { locked = ({}); caps = false }
    }

    // --- typing --------------------------------------------------------------------------
    function hit(spec) {
        const mods = modMask()
        const cmd = mods & (1 | 2 | 4)       // Ctrl, Alt, Super: a shortcut, not text
        switch (spec.k) {
        case "char":
            if (cmd)
                keyboard.press(spec.v, mods & ~16)
            else if (mods & 16)
                { if (spec.a) keyboard.type(spec.a) }
            else
                keyboard.type((spec.l ? shiftOn !== caps : shiftOn) ? spec.s : spec.v)
            break
        case "named": keyboard.press(spec.n, mods & ~16); break
        case "back":
            if (cmd) keyboard.press("BackSpace", mods)
            else keyboard.backspace()
            break
        case "enter":
            if (cmd || (mods & 8)) keyboard.press("Return", mods)
            else keyboard.key("Return")
            break
        case "space":
            if (cmd) keyboard.press("space", mods)
            else keyboard.type(" ")
            break
        case "caps": caps = !caps; return
        }
        keyTyped()
    }
    function repeatsFor(spec) {
        return spec.k === "back" || (spec.k === "named" && ["Left", "Right", "Up", "Down", "Delete",
                                                            "Prior", "Next"].includes(spec.n))
    }

    // --- the keys ------------------------------------------------------------------------
    Item {
        id: board
        width: kb.width; height: kb.height
        y: kb.offset * kb.height

        Rectangle { anchors.fill: parent; color: Theme.bgDark }
        // the hinge's shadow along the top
        Rectangle {
            width: parent.width; height: 10
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(0, 0, 0, 0.35) }
                GradientStop { position: 1; color: "transparent" }
            }
        }

        Repeater {
            model: kb.rows.length
            delegate: Repeater {
                id: rowKeys
                required property int index
                model: kb.rows[index]
                delegate: Rectangle {
                    id: key
                    required property var modelData
                    readonly property var spec: modelData
                    readonly property int row: rowKeys.index
                    property int fingers: 0
                    readonly property bool lit: spec.k === "mod" ? kb.modOn(spec.m)
                                              : spec.k === "caps" ? kb.caps : false
                    // the big Enter: one key over two rows (its upper part a little wider)
                    readonly property bool enterTop: spec.k === "enter" && row === 2
                    x: kb.pad + kb.sideInsets / 2 + (enterTop ? spec.x - 0.25 : spec.x) * kb.unitW
                    y: kb.rowY(row)
                    width: (enterTop ? spec.w + 0.25 : spec.w) * kb.unitW - kb.gap
                    height: spec.k === "enter" ? kb.rowH(row) + (enterTop ? kb.gap + 2 : 0) : kb.rowH(row)
                    radius: 8
                    color: fingers > 0 || (spec.k === "enter" && kb.enterDown) ? Theme.muted
                         : lit ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.25)
                         : spec.k === "char" || spec.k === "space" ? Qt.lighter(Theme.panel, 1.3) : Theme.panel
                    border.color: lit ? Theme.accent : "transparent"
                    // (the lower part of Enter draws its label; the upper part is blank)
                    Text {
                        visible: key.spec.k !== "char" || !key.dual
                        anchors.centerIn: parent
                        anchors.verticalCenterOffset: key.spec.k === "enter" && !key.enterTop ? -kb.unitH * 0.1 : 0
                        color: key.lit ? Theme.accent : Theme.fg
                        font { family: Theme.font; pixelSize: key.row === 0 || key.spec.t && key.spec.t.length > 2 ? 13 : 20 }
                        text: {
                            const s = key.spec
                            switch (s.k) {
                            case "char": return s.l && (kb.shiftOn !== kb.caps) ? s.s : s.v
                            case "named": case "mod": return s.t
                            case "back": return "⌫"
                            case "enter": return key.enterTop ? "" : "⏎"
                            case "caps": return kb.caps ? "⇪ caps" : "caps"
                            default: return ""
                            }
                        }
                    }
                    // a symbol key: its shifted symbol above, as printed on a keyboard
                    readonly property bool dual: spec.k === "char" && !spec.l
                    Column {
                        visible: key.dual
                        anchors.centerIn: parent
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: key.spec.s || ""
                            color: kb.shiftOn ? Theme.fg : Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: key.spec.v || ""
                            color: kb.shiftOn ? Theme.muted : Theme.fg
                            font { family: Theme.font; pixelSize: 17 }
                        }
                    }
                    // AltGr's character, small in the corner
                    Text {
                        visible: key.spec.k === "char" && !!key.spec.a
                        anchors { right: parent.right; bottom: parent.bottom; margins: 5 }
                        text: key.spec.a || ""
                        color: kb.modOn("altgr") ? Theme.accent : Theme.muted
                        font { family: Theme.font; pixelSize: 10 }
                    }
                }
            }
        }
    }
    property bool enterDown: false

    // --- touch: every finger on its own ---------------------------------------------------
    function keyAt(x, y) {
        for (let r = 0; r < rows.length; ++r) {
            const top = rowY(r) - gap / 2, bottom = rowY(r) + rowH(r) + gap / 2
            if (y < top || y > bottom)
                continue
            const ux = (x - pad - sideInsets / 2) / unitW
            for (const s of rows[r]) {
                const left = s.k === "enter" && r === 2 ? s.x - 0.25 : s.x
                if (ux >= left - 0.05 && ux < s.x + s.w + 0.05)
                    return {spec: s, row: r}
            }
        }
        return null
    }
    property var touches: ({})        // point id -> {spec, item}

    MultiPointTouchArea {
        anchors.fill: parent
        mouseEnabled: true
        onPressed: (points) => {
            for (const p of points) {
                const k = kb.keyAt(p.x, p.y - board.y)
                if (!k) continue
                const spec = k.spec
                const item = kb.findKey(spec)
                kb.touches[p.pointId] = {spec: spec, item: item}
                if (item) item.fingers++
                if (spec.k === "enter") kb.enterDown = true
                if (spec.k === "mod") {
                    if (spec.m !== "super" && (!kb.modOn(spec.m) || kb.latched[spec.m] || kb.locked[spec.m]))
                        kb.tapMod(spec.m)
                    const h = Object.assign({}, kb.held)
                    h[spec.m] = (h[spec.m] || 0) + 1
                    kb.held = h
                    const u = Object.assign({}, kb.usedWhileHeld)
                    delete u[spec.m]
                    kb.usedWhileHeld = u
                } else {
                    kb.hit(spec)
                    if (kb.repeatsFor(spec)) { repeat.spec = spec; repeat.count = 0; repeat.interval = 420; repeat.restart() }
                }
            }
        }
        onReleased: (points) => kb.release(points)
        onCanceled: (points) => kb.release(points)
    }
    function release(points) {
        for (const p of points) {
            const t = touches[p.pointId]
            if (!t) continue
            if (t.item) t.item.fingers = Math.max(0, t.item.fingers - 1)
            if (t.spec.k === "enter") enterDown = false
            if (t.spec.k === "mod") {
                const h = Object.assign({}, held)
                h[t.spec.m] = (h[t.spec.m] || 1) - 1
                if (h[t.spec.m] <= 0) {
                    delete h[t.spec.m]
                    // held and used for a key: it was a hold, not a tap
                    if (usedWhileHeld[t.spec.m]) setMod("latched", t.spec.m, false)
                    // Super alone: the app drawer
                    else if (t.spec.m === "super") superTapped()
                }
                held = h
            }
            if (repeat.spec === t.spec) repeat.stop()
            delete touches[p.pointId]
        }
    }
    function findKey(spec) {
        const visit = (item) => {
            for (const c of item.children) {
                if (c.spec === spec) return c
                const f = visit(c)
                if (f) return f
            }
            return null
        }
        return visit(board)
    }
    // held keys repeat: Backspace, Delete, the arrows, PgUp/PgDn
    Timer {
        id: repeat
        property var spec: null
        property int count: 0
        repeat: true
        onTriggered: {
            if (!spec) { stop(); return }
            kb.hit(spec)
            count++
            interval = count > 10 ? 35 : 70
        }
    }
}
