pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Spotify as a cassette deck: what plays is a tape (Cassette) with the reels turning, played
// with a deck's piano keys (eject, back, rewind, play, pause, forward, next) and a counter;
// eject opens the shelf of tapes (the playlists, the liked songs, the albums, a search), and
// a tape picked goes into the deck. The playing is spotify-player's, in the background
// (Spotify: a Connect device "Mobarchy"), so it plays on with the app closed, and the lock
// screen has it too. Its own window (mobarchy-shell --spotify).
Window {
    id: win

    visible: true
    title: qsTr("Spotify")
    color: Theme.bg

    Spotify {
        id: spotify
        watching: win.active || win.visible
        onSignInUrl: (url) => Qt.openUrlExternally(url)
        // the last approval came back: here again, from the browser
        onSignedInBack: {
            win.raise()
            win.requestActivate()
        }
    }

    // what plays, with its progress moving on between the daemon's reports
    readonly property var pb: spotify.playback
    readonly property bool hasTrack: pb.title !== undefined
    readonly property real duration: pb.durationMs || 0
    property real position: 0
    property real reportedAt: 0
    Connections {
        target: spotify
        function onPlaybackChanged() {
            win.position = win.pb.progressMs || 0
            win.reportedAt = Date.now()
        }
    }
    Timer {
        interval: 100; repeat: true
        running: win.pb.playing === true && !scrub.pressed
        onTriggered: {
            const now = Date.now()
            win.position = Math.min(win.duration, win.position + (now - win.reportedAt))
            win.reportedAt = now
        }
    }
    function time(ms) {
        const s = Math.floor((ms || 0) / 1000)
        return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0")
    }
    // a tape's colour, from its name
    function colourOf(name) {
        const colours = ["#e06c75", "#e5c07b", "#98c379", "#56b6c2", "#61afef", "#c678dd", "#d19a66", "#f7768e"]
        let h = 0
        for (const c of (name || "")) h = (h * 31 + c.charCodeAt(0)) >>> 0
        return colours[h % colours.length]
    }

    // the shelf: tapes to pick (shown over the deck)
    property bool shelf: false
    property string tab: "playlists"     // playlists, liked, albums, search
    property bool tracks: false          // a tape's songs (spotify.opened)

    // --- signed out --------------------------------------------------------------------------
    Column {
        visible: spotify.state === "signedOut" || spotify.state === "signingIn"
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 24
        Cassette {
            width: parent.width
            title: qsTr("Mobarchy mixtape")
            artists: qsTr("Sign in to play")
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: spotify.state === "signingIn"
                  ? qsTr("Approve Mobarchy in the browser. Spotify asks twice: once for your library, once to play.")
                  : qsTr("Play Spotify on this phone. Needs Spotify Premium.")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 14 }
        }
        Rectangle {
            visible: spotify.state === "signedOut"
            anchors.horizontalCenter: parent.horizontalCenter
            width: signInText.implicitWidth + 48; height: 48; radius: 24
            color: signInTap.pressed ? Qt.darker("#1db954", 1.2) : "#1db954"
            Text {
                id: signInText
                anchors.centerIn: parent
                text: qsTr("Sign in with Spotify")
                color: "#0b0b0f"
                font { family: Theme.font; pixelSize: 15; bold: true }
            }
            MouseArea { id: signInTap; anchors.fill: parent; onClicked: spotify.signIn() }
        }
    }

    // --- the deck ------------------------------------------------------------------------------
    Item {
        id: deck
        visible: spotify.state === "ready" || spotify.state === "starting"
        anchors.fill: parent

        Column {
            anchors { top: parent.top; topMargin: 24; horizontalCenter: parent.horizontalCenter }
            width: Math.min(parent.width - 32, 520)
            spacing: 22

            // the deck's door, with the tape behind its glass
            Rectangle {
                id: door
                width: parent.width
                height: tapeView.height + 36
                radius: 18
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(Theme.panel, 1.25) }
                    GradientStop { position: 1; color: Theme.panel }
                }
                border.color: Theme.line
                Cassette {
                    id: tapeView
                    anchors.centerIn: parent
                    width: parent.width - 36
                    progress: win.duration > 0 ? win.position / win.duration : 0
                    playing: win.pb.playing === true
                    title: win.pb.title || ""
                    artists: win.pb.artists || ""
                    image: win.pb.image || ""
                    stripe: win.colourOf(win.pb.album || win.pb.title)
                    // a new tape: in from below
                    property string loaded: win.pb.trackId || ""
                    onLoadedChanged: insert.restart()
                    transform: Translate { id: shift; y: 0 }
                    SequentialAnimation {
                        id: insert
                        NumberAnimation { target: shift; property: "y"; to: door.height; duration: 160; easing.type: Easing.InCubic }
                        NumberAnimation { target: shift; property: "y"; from: door.height; to: 0; duration: 380; easing.type: Easing.OutBack }
                    }
                }
                // the glass of the door
                Rectangle {
                    anchors.fill: parent
                    radius: parent.radius
                    color: "transparent"
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.05) }
                        GradientStop { position: 0.3; color: Qt.rgba(1, 1, 1, 0.0) }
                        GradientStop { position: 0.55; color: Qt.rgba(1, 1, 1, 0.06) }
                        GradientStop { position: 0.7; color: Qt.rgba(1, 1, 1, 0.0) }
                    }
                }
            }

            // the counter and the tape's way through (drag to wind)
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 14
                Rectangle {
                    width: counterText.implicitWidth + 20; height: 34; radius: 6
                    color: "#0b0b0f"
                    border.color: Theme.line
                    Text {
                        id: counterText
                        anchors.centerIn: parent
                        text: win.time(win.position)
                        color: "#ff9e64"
                        font { family: Theme.font; pixelSize: 20; bold: true }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "/ " + win.time(win.duration)
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 14 }
                }
            }
            Item {
                width: parent.width; height: 28
                Rectangle {
                    id: groove
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width; height: 6; radius: 3
                    color: Theme.panel
                    Rectangle {
                        width: groove.width * (win.duration > 0 ? win.position / win.duration : 0)
                        height: parent.height; radius: 3
                        color: "#4a2f1f"
                        border.color: "#6b4630"
                    }
                }
                Rectangle {
                    x: groove.width * (win.duration > 0 ? win.position / win.duration : 0) - width / 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 18; height: 18; radius: 9
                    color: Theme.fg
                }
                MouseArea {
                    id: scrub
                    anchors.fill: parent
                    enabled: win.duration > 0
                    function at(mx) { return Math.max(0, Math.min(1, mx / width)) * win.duration }
                    onPressed: (m) => win.position = at(m.x)
                    onPositionChanged: (m) => win.position = at(m.x)
                    onReleased: (m) => { spotify.seekTo(at(m.x)); win.reportedAt = Date.now() }
                }
            }

            // the piano keys
            Row {
                id: keys
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 6
                readonly property real keyWidth: (deck.width > 560 ? 520 : deck.width - 32 - 6 * spacing) / 7
                DeckKey { icon: "󰐊"; label: "EJECT"; glyph: "⏏"; onPushed: win.shelf = true }
                DeckKey { icon: "󰒮"; label: "PREV"; onPushed: spotify.previous() }
                DeckKey { icon: "󰑟"; label: "REW"; onPushed: { spotify.seekTo(Math.max(0, win.position - 10000)); win.position = Math.max(0, win.position - 10000) } }
                DeckKey { icon: "󰐊"; label: "PLAY"; latched: win.pb.playing === true; onPushed: if (win.pb.playing !== true) spotify.playPause() }
                DeckKey { icon: "󰏤"; label: "PAUSE"; latched: win.hasTrack && win.pb.playing !== true; onPushed: if (win.pb.playing === true) spotify.playPause() }
                DeckKey { icon: "󰈑"; label: "FF"; onPushed: { spotify.seekTo(Math.min(win.duration, win.position + 10000)); win.position = Math.min(win.duration, win.position + 10000) } }
                DeckKey { icon: "󰒭"; label: "NEXT"; onPushed: spotify.next() }
            }

            // shuffle, repeat, like
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 28
                Toggle { icon: "󰒟"; on: win.pb.shuffle === true; onTapped: spotify.toggleShuffle() }
                Toggle {
                    icon: win.pb.repeat === "track" ? "󰑘" : "󰑖"
                    on: win.pb.repeat === "track" || win.pb.repeat === "context"
                    onTapped: spotify.cycleRepeat()
                }
                Toggle { icon: "󰋑"; on: false; onTapped: { spotify.like(true); win.toast(qsTr("Added to Liked Songs")) } }
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !win.hasTrack
                text: qsTr("No tape in. Press EJECT to pick one.")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }
    }

    component DeckKey: Item {
        id: dk
        property string icon
        property string glyph
        property string label
        property bool latched: false        // stays down (PLAY while it plays)
        signal pushed()
        width: keys.keyWidth; height: 78
        readonly property bool down: tap.pressed || latched
        Rectangle {
            id: cap
            width: parent.width; height: parent.height - 8
            y: dk.down ? 7 : 0
            Behavior on y { NumberAnimation { duration: 70 } }
            radius: 6
            gradient: Gradient {
                GradientStop { position: 0; color: dk.down ? "#3a3a46" : "#5a5a68" }
                GradientStop { position: 1; color: dk.down ? "#24242c" : "#34343f" }
            }
            border.color: "#14141a"
            Text {
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 12 }
                text: dk.glyph || dk.icon
                color: dk.latched ? Theme.accent : Theme.fg
                font { family: Theme.font; pixelSize: 22 }
            }
            Text {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 8 }
                text: dk.label
                color: Theme.muted
                font { family: Theme.font; pixelSize: 9; bold: true; letterSpacing: 1 }
            }
        }
        // the key's side, under it
        Rectangle {
            z: -1
            width: parent.width; height: parent.height - 2; y: 2
            radius: 6
            color: "#101015"
        }
        MouseArea { id: tap; anchors.fill: parent; onClicked: dk.pushed() }
    }

    component Toggle: Text {
        id: tg
        property string icon
        property bool on: false
        signal tapped()
        text: icon
        color: on ? Theme.accent : Theme.muted
        font { family: Theme.font; pixelSize: 24 }
        MouseArea { anchors { fill: parent; margins: -12 } onClicked: tg.tapped() }
    }

    // --- the shelf -----------------------------------------------------------------------------
    Rectangle {
        id: shelfView
        width: parent.width; height: parent.height
        color: Theme.bg
        visible: y < height
        y: win.shelf ? 0 : height
        Behavior on y { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

        Item {
            id: shelfHead
            width: parent.width; height: 56
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: win.tracks ? "  " + (spotify.opened.name || "") : qsTr("Tapes")
                elide: Text.ElideRight
                width: parent.width - 80
                color: Theme.fg
                font { family: Theme.font; pixelSize: 18; bold: true }
                MouseArea { anchors.fill: parent; enabled: win.tracks; onClicked: win.tracks = false }
            }
            // sign out (a tap, then a second to confirm)
            Text {
                id: signOut
                property bool confirm: false
                visible: !win.tracks
                anchors { right: closeShelf.left; rightMargin: 20; verticalCenter: parent.verticalCenter }
                text: confirm ? qsTr("Sign out?") : "󰍃"
                color: confirm ? Theme.red : Theme.muted
                font { family: Theme.font; pixelSize: confirm ? 14 : 20 }
                MouseArea {
                    anchors { fill: parent; margins: -12 }
                    onClicked: {
                        if (signOut.confirm) {
                            signOut.confirm = false
                            win.shelf = false
                            spotify.signOut()
                        } else {
                            signOut.confirm = true
                            signOutReset.restart()
                        }
                    }
                }
                Timer { id: signOutReset; interval: 3000; onTriggered: signOut.confirm = false }
            }
            Text {
                id: closeShelf
                anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                text: "󰅖"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 22 }
                MouseArea { anchors { fill: parent; margins: -12 } onClicked: { win.shelf = false; win.tracks = false } }
            }
        }

        // the tabs
        Row {
            id: tabs
            visible: !win.tracks
            anchors { top: shelfHead.bottom; left: parent.left; leftMargin: 12 }
            spacing: 8
            Repeater {
                model: [{k: "playlists", t: qsTr("Playlists")}, {k: "liked", t: qsTr("Liked")},
                        {k: "albums", t: qsTr("Albums")}, {k: "search", t: "󰍉"}]
                delegate: Rectangle {
                    id: tabChip
                    required property var modelData
                    readonly property bool chosen: win.tab === modelData.k
                    width: tabText.implicitWidth + 28; height: 36; radius: 18
                    color: chosen ? Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16) : Theme.panel
                    border.color: chosen ? Theme.accent : Theme.line
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: tabChip.modelData.t
                        color: tabChip.chosen ? Theme.accent : Theme.fg
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    MouseArea { anchors.fill: parent; onClicked: win.tab = tabChip.modelData.k }
                }
            }
        }

        // a search
        Rectangle {
            id: searchBox
            visible: !win.tracks && win.tab === "search"
            anchors { top: tabs.bottom; topMargin: 12; left: parent.left; right: parent.right; margins: 12 }
            height: visible ? 44 : 0
            radius: 22
            color: Theme.panel
            border.color: Theme.line
            TextInput {
                id: query
                anchors { fill: parent; leftMargin: 18; rightMargin: 18 }
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
                onAccepted: spotify.search(text)
                onTextChanged: searchDelay.restart()
                Text { visible: parent.text === ""; anchors.verticalCenter: parent.verticalCenter
                       text: qsTr("Songs, artists, albums"); color: Theme.muted; font: parent.font }
            }
            Timer { id: searchDelay; interval: 500; onTriggered: spotify.search(query.text) }
        }

        // play the tape that is open: in order, or shuffled
        Row {
            id: tapeActions
            visible: win.tracks || (win.tab === "liked" && !win.tracks)
            anchors { top: win.tracks ? shelfHead.bottom : tabs.bottom; topMargin: 10; left: parent.left; leftMargin: 12 }
            height: visible ? 40 : 0
            spacing: 10
            Repeater {
                model: [{shuffle: false, t: "󰐊  " + qsTr("Play")}, {shuffle: true, t: "󰒟  " + qsTr("Shuffle")}]
                delegate: Rectangle {
                    id: act
                    required property var modelData
                    width: actText.implicitWidth + 32; height: 40; radius: 20
                    color: actTap.pressed ? Qt.darker("#1db954", 1.2) : (modelData.shuffle ? Theme.panel : "#1db954")
                    Text {
                        id: actText
                        anchors.centerIn: parent
                        text: act.modelData.t
                        color: act.modelData.shuffle ? Theme.fg : "#0b0b0f"
                        font { family: Theme.font; pixelSize: 14; bold: !act.modelData.shuffle }
                    }
                    MouseArea {
                        id: actTap
                        anchors.fill: parent
                        onClicked: {
                            if (win.tracks)
                                spotify.playContext(spotify.opened.type, spotify.opened.id, act.modelData.shuffle)
                            else
                                spotify.playLiked(act.modelData.shuffle)
                            win.shelf = false
                            win.tracks = false
                        }
                    }
                }
            }
        }

        ListView {
            id: list
            anchors { top: tapeActions.visible ? tapeActions.bottom : (searchBox.visible ? searchBox.bottom : tabs.bottom)
                      topMargin: 10; left: parent.left; right: parent.right; bottom: parent.bottom }
            clip: true
            spacing: 8
            boundsBehavior: Theme.boundsBehavior
            flickDeceleration: Theme.flickDeceleration
            maximumFlickVelocity: Theme.flickVelocity
            // rows: {kind: "playlist"|"album"|"track"|"heading", ...}
            model: {
                if (win.tracks)
                    return (spotify.opened.tracks || []).map(t => Object.assign({kind: "track"}, t))
                switch (win.tab) {
                case "playlists": return spotify.playlists.map(p => Object.assign({kind: "playlist"}, p))
                case "liked": return spotify.liked.map(t => Object.assign({kind: "track"}, t))
                case "albums": return spotify.albums.map(a => Object.assign({kind: "album"}, a))
                case "search": {
                    const r = spotify.results
                    const rows = []
                    if ((r.tracks || []).length) rows.push({kind: "heading", name: qsTr("Songs")})
                    for (const t of (r.tracks || []).slice(0, 10)) rows.push(Object.assign({kind: "track"}, t))
                    if ((r.albums || []).length) rows.push({kind: "heading", name: qsTr("Albums")})
                    for (const a of (r.albums || []).slice(0, 8)) rows.push(Object.assign({kind: "album"}, a))
                    if ((r.playlists || []).length) rows.push({kind: "heading", name: qsTr("Playlists")})
                    for (const p of (r.playlists || []).slice(0, 8)) rows.push(Object.assign({kind: "playlist"}, p))
                    return rows
                }
                }
                return []
            }
            delegate: Item {
                id: row
                required property var modelData
                width: list.width
                height: modelData.kind === "heading" ? 34 : 60

                Text {
                    visible: row.modelData.kind === "heading"
                    anchors { left: parent.left; leftMargin: 16; bottom: parent.bottom; bottomMargin: 4 }
                    text: row.modelData.name
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13; bold: true }
                }
                // a tape on the shelf: its spine, with a label
                Rectangle {
                    visible: row.modelData.kind !== "heading"
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    radius: 8
                    color: rowTap.pressed ? Theme.line : "#2a2a33"
                    border.color: "#3a3a46"
                    Rectangle {
                        id: spine
                        width: 8; height: parent.height - 16; radius: 3
                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        color: win.colourOf(row.modelData.album || row.modelData.name)
                    }
                    Rectangle {
                        id: cover
                        visible: (row.modelData.image || "") !== ""
                        anchors { left: spine.right; leftMargin: 8; verticalCenter: parent.verticalCenter }
                        width: 44; height: 44; radius: 4
                        clip: true
                        color: "#15151b"
                        Image { anchors.fill: parent; source: row.modelData.image || ""; asynchronous: true
                                fillMode: Image.PreserveAspectCrop; sourceSize.width: 88 }
                    }
                    Rectangle {
                        anchors { left: cover.visible ? cover.right : spine.right; leftMargin: 10; right: parent.right
                                  rightMargin: 10; verticalCenter: parent.verticalCenter }
                        height: 42; radius: 3
                        color: "#efe6d2"
                        Column {
                            anchors { left: parent.left; right: parent.right; leftMargin: 10; rightMargin: 10
                                      verticalCenter: parent.verticalCenter }
                            Text {
                                width: parent.width
                                text: row.modelData.name || ""
                                elide: Text.ElideRight
                                color: "#22232d"
                                font { family: Theme.font; pixelSize: 14; bold: true; italic: true }
                            }
                            Text {
                                width: parent.width
                                text: row.modelData.kind === "track"
                                      ? (row.modelData.artists || "") + (row.modelData.durationMs ? "  ·  " + win.time(row.modelData.durationMs) : "")
                                      : row.modelData.kind === "playlist" ? (row.modelData.owner || "") : (row.modelData.artists || "")
                                elide: Text.ElideRight
                                color: "#55566a"
                                font { family: Theme.font; pixelSize: 11; italic: true }
                            }
                        }
                    }
                    MouseArea {
                        id: rowTap
                        anchors.fill: parent
                        onClicked: {
                            const m = row.modelData
                            if (m.kind === "track") {
                                spotify.playTrack(m.id)
                                win.shelf = false
                                win.tracks = false
                            } else {
                                spotify.open(m.kind, m.id, m.name)
                                win.tracks = true
                            }
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: list.count === 0
                text: win.tracks && spotify.opened.loading ? qsTr("Loading …")
                    : win.tab === "search" ? qsTr("Search for something to play")
                    : qsTr("Nothing here yet")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
        }
    }

    // --- a word, for a moment ------------------------------------------------------------------
    function toast(text) {
        toastText.text = text
        toastBox.opacity = 1
        toastFade.restart()
    }
    Rectangle {
        id: toastBox
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 90 }
        width: Math.min(toastText.implicitWidth + 32, parent.width - 40); height: toastText.implicitHeight + 20; radius: 20
        color: Theme.fg
        opacity: 0
        z: 6
        Text {
            id: toastText
            anchors.centerIn: parent
            color: Theme.bg
            font { family: Theme.font; pixelSize: 14 }
        }
        SequentialAnimation {
            id: toastFade
            PauseAnimation { duration: 1800 }
            NumberAnimation { target: toastBox; property: "opacity"; to: 0; duration: 300 }
        }
    }
    // the daemon's trouble, said
    Connections {
        target: spotify
        function onErrorChanged() { if (spotify.error) win.toast(spotify.error) }
    }
}
