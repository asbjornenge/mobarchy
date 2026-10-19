pragma ComponentBehavior: Bound

import QtQuick
import QtMultimedia
import Mobarchy.Shell

// Phone (ADR 0031): the dialer, the call, and the SIM's PIN, over ofono (Telephony). Its own
// window (mobarchy-shell --phone), like the Clock. A locked SIM asks for its PIN first; then
// it shows the operator and signal, a keypad to dial, and — while a call is up — the in-call
// screen. An incoming call takes over with answer/decline.
Window {
    id: win

    visible: true
    title: qsTr("Phone")
    color: Theme.bg

    Telephony {
        id: tel
        onFailed: (what, reason) => { win.note = what + ": " + reason; noteClear.restart() }
    }

    property string entry: ""        // the number being dialled
    property string note: ""
    Timer { id: noteClear; interval: 4000; onTriggered: win.note = "" }

    readonly property var incoming: {
        for (let i = 0; i < tel.calls.length; i++)
            if (tel.calls[i].incoming) return tel.calls[i]
        return null
    }
    readonly property var activeCall: tel.calls.length > 0 ? tel.calls[tel.calls.length - 1] : null

    // the ringback tone while an outgoing call waits for an answer (the phone makes its own:
    // the network often plays none); call audio routes it to the earpiece from dialing on
    readonly property bool waiting: activeCall !== null
        && (activeCall.state === "dialing" || activeCall.state === "alerting")
    MediaPlayer {
        id: ringback
        source: "qrc:/qt/qml/Mobarchy/Shell/data/sounds/ringback.wav"
        loops: MediaPlayer.Infinite
        audioOutput: AudioOutput { volume: 0.5 }
    }
    onWaitingChanged: waiting ? ringback.play() : ringback.stop()

    function stateText(s) {
        return ({ dialing: qsTr("Calling…"), alerting: qsTr("Ringing…"), active: qsTr("In call"),
                  held: qsTr("On hold"), incoming: qsTr("Incoming"), waiting: qsTr("Waiting") })[s] ?? s
    }

    // a little message at the top (errors)
    Text {
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 8 }
        visible: win.note !== ""
        text: win.note
        color: Theme.red
        font { family: Theme.font; pixelSize: 13 }
    }

    // 1) a locked SIM: enter the PIN
    SimPin {
        anchors.fill: parent
        visible: tel.pinRequired === "pin" || tel.pinRequired === "puk"
        telephony: tel
    }

    // 2) an incoming call
    Loader {
        anchors.fill: parent
        active: win.incoming !== null
        sourceComponent: incomingScreen
    }

    // 3) an ongoing call
    Loader {
        anchors.fill: parent
        active: win.incoming === null && win.activeCall !== null
        sourceComponent: callScreen
    }

    // 4) the phone: the keypad, the contacts and the recent calls, in tabs
    property string tab: "keypad"
    property var recents: []
    function reloadRecents() { recents = tel.recentCalls() }
    Connections { target: tel; function onCallsChanged() { Qt.callLater(win.reloadRecents) } }
    Component.onCompleted: reloadRecents()
    function call(number) { if (number !== "") { tel.dial(number); win.entry = "" } }
    function timeText(epoch) {
        const d = new Date(epoch * 1000), now = new Date()
        return d.toDateString() === now.toDateString() ? Qt.formatTime(d, "HH:mm")
             : Qt.formatDate(d, "d. MMM")
    }

    Contacts { id: contacts }

    Item {
        anchors.fill: parent
        visible: tel.pinRequired === "none" && tel.calls.length === 0

        // the operator
        Text {
            id: operatorLine
            anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 24 }
            text: tel.registration === "registered" || tel.registration === "roaming"
                  ? (tel.operatorName !== "" ? tel.operatorName : qsTr("Mobile"))
                  : (tel.registration === "searching" ? qsTr("Searching…") : qsTr("No service"))
            color: tel.registration === "registered" || tel.registration === "roaming" ? Theme.muted : Theme.red
            font { family: Theme.font; pixelSize: 14 }
        }

        // --- the keypad ---
        Item {
            anchors { left: parent.left; right: parent.right; top: operatorLine.bottom; bottom: tabs.top }
            visible: win.tab === "keypad"

            Column {
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 50 }
                width: parent.width - 80
                spacing: 6
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: win.entry
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 40 }
                    elide: Text.ElideLeft
                }
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: win.entry.length >= 6 ? contacts.nameFor(win.entry) : ""
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16 }
                }
            }

            Grid {
                id: pad
                anchors { horizontalCenter: parent.horizontalCenter; bottom: callRow.top; bottomMargin: 20 }
                columns: 3
                spacing: 18
                readonly property var keys: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"]
                readonly property var sub: { "2": "ABC", "3": "DEF", "4": "GHI", "5": "JKL", "6": "MNO",
                                             "7": "PQRS", "8": "TUV", "9": "WXYZ", "0": "+" }
                Repeater {
                    model: pad.keys
                    delegate: Rectangle {
                        id: key
                        required property string modelData
                        width: 76; height: 76; radius: 38
                        color: keyTap.pressed ? Theme.line : Theme.panel
                        Column {
                            anchors.centerIn: parent
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: key.modelData
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 30 }
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: pad.sub[key.modelData] ?? ""
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 9 }
                            }
                        }
                        TapHandler {
                            id: keyTap
                            onTapped: win.entry += key.modelData
                            onLongPressed: if (key.modelData === "0") win.entry += "+"
                        }
                    }
                }
            }

            Row {
                id: callRow
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 18 }
                spacing: 26
                Item { width: 60; height: 60 }   // to centre the call button
                Rectangle {
                    width: 72; height: 72; radius: 36
                    color: win.entry === "" ? Theme.line : Theme.green
                    Text { anchors.centerIn: parent; text: "󰏲"; color: Theme.bg; font { family: Theme.font; pixelSize: 30 } }
                    TapHandler { onTapped: win.call(win.entry) }
                }
                Rectangle {
                    width: 60; height: 60; radius: 30
                    color: "transparent"
                    visible: win.entry !== ""
                    Text { anchors.centerIn: parent; text: "⌫"; color: Theme.muted; font.pixelSize: 26 }
                    TapHandler {
                        onTapped: win.entry = win.entry.slice(0, -1)
                        onLongPressed: win.entry = ""
                    }
                }
            }
        }

        // --- the contacts ---
        ContactList {
            anchors { left: parent.left; right: parent.right; top: operatorLine.bottom; topMargin: 12; bottom: tabs.top }
            visible: win.tab === "contacts"
            book: contacts
            onCall: (number) => win.call(number)
            onMessage: (number) => Messaging.open(number)
        }

        // --- the recent calls ---
        ListView {
            anchors { left: parent.left; right: parent.right; top: operatorLine.bottom; topMargin: 12; bottom: tabs.top }
            visible: win.tab === "recents"
            clip: true
            boundsBehavior: Theme.boundsBehavior
            model: win.recents
            delegate: Rectangle {
                id: rec
                required property var modelData
                readonly property bool missed: !modelData.outgoing && !modelData.answered
                readonly property string who: contacts.nameFor(modelData.number) || modelData.number
                width: ListView.view.width; height: 60
                color: recTap.pressed ? Theme.panel : "transparent"
                Text {
                    x: 20; anchors.verticalCenter: parent.verticalCenter
                    text: rec.modelData.outgoing ? "󰁜" : "󰁆"   // ↗ out, ↙ in
                    color: rec.missed ? Theme.red : Theme.muted
                    font { family: Theme.font; pixelSize: 18 }
                }
                Column {
                    x: 56; anchors.verticalCenter: parent.verticalCenter
                    Text {
                        text: rec.who
                        color: rec.missed ? Theme.red : Theme.fg
                        font { family: Theme.font; pixelSize: 16 }
                    }
                    Text {
                        text: rec.missed ? qsTr("Missed") : rec.modelData.seconds > 0
                              ? Math.floor(rec.modelData.seconds / 60) + ":" + String(rec.modelData.seconds % 60).padStart(2, "0")
                              : (rec.modelData.outgoing ? qsTr("No answer") : "")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 12 }
                    }
                }
                Text {
                    anchors { right: parent.right; rightMargin: 24; verticalCenter: parent.verticalCenter }
                    text: win.timeText(rec.modelData.time)
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 13 }
                }
                TapHandler { id: recTap; onTapped: win.call(rec.modelData.number) }
            }
            Text {
                anchors.centerIn: parent
                visible: win.recents.length === 0
                text: qsTr("No calls yet")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 15 }
            }
        }

        // the tabs
        Row {
            id: tabs
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 64
            Repeater {
                model: [{ id: "keypad", label: qsTr("Keypad"), icon: "󰌌" },
                        { id: "contacts", label: qsTr("Contacts"), icon: "󰀉" },
                        { id: "recents", label: qsTr("Recent"), icon: "󰋚" }]
                delegate: Item {
                    id: tabItem
                    required property var modelData
                    width: tabs.width / 3; height: tabs.height
                    readonly property bool on: win.tab === modelData.id
                    Column {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: tabItem.modelData.icon
                            color: tabItem.on ? Theme.accent : Theme.muted
                            font { family: Theme.font; pixelSize: 22 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: tabItem.modelData.label
                            color: tabItem.on ? Theme.accent : Theme.muted
                            font { family: Theme.font; pixelSize: 11 }
                        }
                    }
                    TapHandler { onTapped: { win.tab = tabItem.modelData.id; if (tabItem.modelData.id === "recents") win.reloadRecents() } }
                }
            }
        }
    }

    // --- incoming call ---
    Component {
        id: incomingScreen
        Item {
            readonly property var c: win.incoming
            Column {
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 140 }
                spacing: 10
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: parent.parent.c ? (contacts.nameFor(parent.parent.c.number) || parent.parent.c.name
                                           || parent.parent.c.number) : ""
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 34 }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Incoming call")
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 15 }
                }
            }
            Row {
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 60 }
                spacing: 80
                Rectangle {
                    width: 78; height: 78; radius: 39; color: Theme.red
                    Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 30 }
                    TapHandler { onTapped: if (win.incoming) tel.hangup(win.incoming.path) }
                }
                Rectangle {
                    width: 78; height: 78; radius: 39; color: Theme.green
                    Text { anchors.centerIn: parent; text: "📞"; font.pixelSize: 30 }
                    TapHandler { onTapped: if (win.incoming) tel.answer(win.incoming.path) }
                }
            }
        }
    }

    // --- in call ---
    Component {
        id: callScreen
        Item {
            id: inCall
            readonly property var c: win.activeCall
            property bool showPad: false
            property string tones: ""   // what was keyed in this call (an answering machine's menu)
            Column {
                visible: !inCall.showPad
                anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 150 }
                spacing: 12
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: parent.parent.c ? (contacts.nameFor(parent.parent.c.number) || parent.parent.c.name
                                           || parent.parent.c.number) : ""
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 34 }
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: parent.parent.c ? win.stateText(parent.parent.c.state) : ""
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 16 }
                }
            }
            // the keypad: tones to the other end (DTMF)
            Column {
                visible: inCall.showPad
                anchors { horizontalCenter: parent.horizontalCenter; bottom: options.top; bottomMargin: 30 }
                spacing: 16
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(inCall.width - 40, 320)
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideLeft
                    text: inCall.tones
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 32 }
                }
                Grid {
                    id: tonePad
                    columns: 3
                    spacing: 16
                    Repeater {
                        model: ["1", "2", "3", "4", "5", "6", "7", "8", "9", "*", "0", "#"]
                        delegate: Rectangle {
                            id: toneKey
                            required property string modelData
                            width: 72; height: 72; radius: 36
                            color: toneTap.pressed ? Theme.line : Theme.panel
                            Text {
                                anchors.centerIn: parent
                                text: toneKey.modelData
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 30 }
                            }
                            TapHandler {
                                id: toneTap
                                onTapped: {
                                    tel.sendTones(toneKey.modelData)
                                    inCall.tones += toneKey.modelData
                                }
                            }
                        }
                    }
                }
            }
            // the keypad, and mute and speaker (when the device has call audio)
            Row {
                id: options
                anchors { horizontalCenter: parent.horizontalCenter; bottom: hangupButton.top; bottomMargin: 40 }
                spacing: 48
                Repeater {
                    model: [
                        { label: qsTr("Keypad"), icon: "󰌌", on: inCall.showPad, toggle: () => inCall.showPad = !inCall.showPad }
                    ].concat(tel.callAudio ? [
                        { label: qsTr("Mute"), icon: "󰍭", on: tel.muted, toggle: () => tel.setMuted(!tel.muted) },
                        { label: qsTr("Speaker"), icon: "󰕾", on: tel.speaker, toggle: () => tel.setSpeaker(!tel.speaker) }
                    ] : [])
                    delegate: Column {
                        id: opt
                        required property var modelData
                        spacing: 8
                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: 68; height: 68; radius: 34
                            color: opt.modelData.on ? Theme.fg : Theme.panel
                            Text {
                                anchors.centerIn: parent
                                text: opt.modelData.icon
                                color: opt.modelData.on ? Theme.bg : Theme.fg
                                font { family: Theme.font; pixelSize: 28 }
                            }
                            TapHandler { onTapped: opt.modelData.toggle() }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: opt.modelData.label
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                    }
                }
            }
            Rectangle {
                id: hangupButton
                anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 60 }
                width: 78; height: 78; radius: 39; color: Theme.red
                Text { anchors.centerIn: parent; text: "✕"; color: "white"; font.pixelSize: 30 }
                TapHandler { onTapped: tel.hangupAll() }
            }
        }
    }
}
