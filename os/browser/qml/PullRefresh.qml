import QtQuick
import QtQuick.Shapes
import QtWebEngine

// Pull to refresh, as Mail does it: a finger pulling down on a page that is at its top.
// Bifrost's rainbow comes out of the top, growing with the pull, its arcs drawn out one by
// one, and the page slides down to make room; let go with it whole and the page loads
// again, a wave running through the arcs until it has.
// It only watches (the page keeps its touches): a pull starts where nothing on the page can
// scroll up any more (the page at its top, and every scrolling box under the finger at its
// own: a chat's list, scrolled up, is not a pull).
Item {
    id: pull

    property WebEngineView view
    // the pages' item: moved down with the pull
    property Item page
    // how far the finger has to come
    readonly property real far: 120
    property bool allowed: false
    property real startY: 0
    property real pulled: 0
    property bool refreshing: false
    readonly property real t: Math.min(1, pulled / far)
    readonly property bool armed: pulled >= far
    // the room made at the top
    readonly property real room: refreshing ? 64 : Math.min(80, pulled * 0.55)

    anchors.fill: parent

    Binding {
        target: pull.page
        property: "y"
        value: pull.room
    }
    Behavior on room { enabled: !finger.active; NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }

    PointHandler {
        id: finger
        acceptedDevices: PointerDevice.TouchScreen
        onActiveChanged: {
            if (active) {
                pull.startY = point.position.y
                pull.pulled = 0
                pull.allowed = false
                if (pull.view && pull.view.scrollPosition.y <= 1 && !pull.refreshing)
                    pull.check(point.position.x, point.position.y)
                return
            }
            if (pull.allowed && pull.armed && pull.view) {
                pull.refreshing = true
                pull.view.reload()
            }
            pull.allowed = false
            pull.pulled = 0
        }
        onPointChanged: if (active && pull.allowed) pull.pulled = Math.max(0, point.position.y - pull.startY)
    }

    // whether anything under (x, y) can still scroll up: if not, a pull may start
    function check(x, y) {
        const js = "(function(x, y) {"
                 + " if ((document.scrollingElement || document.documentElement).scrollTop > 0) return false;"
                 + " for (let e = document.elementFromPoint(x, y); e; e = e.parentElement)"
                 + "   if (e.scrollTop > 0) return false;"
                 + " return true; })(" + x + ", " + y + ")"
        const at = view
        view.runJavaScript(js, (ok) => { if (finger.active && at === pull.view) pull.allowed = ok === true })
    }

    Connections {
        target: pull.view
        function onLoadingChanged(info) {
            if (info.status !== WebEngineView.LoadStartedStatus)
                pull.refreshing = false
        }
    }

    // the wave through the arcs while the page loads (0..1, round and round)
    property real wave: 0
    NumberAnimation on wave {
        running: pull.refreshing
        from: 0; to: 1
        duration: 1100
        loops: Animation.Infinite
    }

    // the rainbow, in the room at the top
    Item {
        id: icon
        visible: pull.room > 4
        width: 48; height: 30
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(0, (pull.room - height) / 2)
        scale: pull.refreshing ? 1 : 0.3 + 0.7 * pull.t
        Behavior on scale { NumberAnimation { duration: 120 } }

        Repeater {
            model: 5
            delegate: Shape {
                id: arc
                required property int index
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                readonly property real r: 22 - index * 4
                // drawn out as the pull passes its share (the outer arc first); while the
                // page loads, whole, a wave of light running through them
                readonly property real drawn: pull.refreshing ? 1 : Math.max(0, Math.min(1, pull.t * 1.6 - index * 0.15))
                readonly property real angle: Math.PI * (1 - drawn)
                opacity: pull.refreshing ? 0.35 + 0.65 * (0.5 + 0.5 * Math.cos(2 * Math.PI * (pull.wave - index / 5)))
                                         : (pull.armed ? 1 : 0.55)
                visible: drawn > 0.01
                ShapePath {
                    strokeWidth: 3.5
                    strokeColor: ["#f7768e", "#e0af68", "#9ece6a", "#7aa2f7", "#bb9af7"][arc.index]
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    startX: 24 - arc.r; startY: 27
                    PathArc {
                        x: 24 + arc.r * Math.cos(arc.angle)
                        y: 27 - arc.r * Math.sin(arc.angle)
                        radiusX: arc.r; radiusY: arc.r
                    }
                }
            }
        }
    }
}
