pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Mobarchy.Shell

// Home: the agent. A feed of what it says and does, and a field to ask it something.
// A normal window, not a layer surface: text input (and the on-screen keyboard) only
// works there on Hyprland (ADR 0006).
//
// Which agent answers is the owner's choice (ADR 0010): "Choose your agent" until one is
// set up, then a chip at the top shows the active one and switches between them.
Window {
    id: home

    required property Agent agent
    required property Talk talk
    required property Keyboard keyboard
    // Text selected in the conversation: the shell's Copy button offers it.
    signal copyOffered(string text)
    property bool dragging: false   // a selection handle is being dragged

    title: "Mobarchy"
    color: Theme.bg
    Component.onCompleted: show()

    // The theme's wallpaper (ADR 0025), dimmed further than the lock screen's: the
    // conversation is text to read.
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
        opacity: 0.7
    }

    readonly property var current: agentById(agent.active)
    readonly property bool chosen: current !== undefined && current.ready
    property bool picking: false
    property bool browsing: false  // the list of conversations

    function agentById(id) {
        return agent.agents.find(a => a.id === id)
    }
    readonly property var agentIcons: ({ claude: "󰚩", openai: "󰧑", local: "󰍹", api: "󰌆" })
    // Names for conversation history, also before the agent list has loaded.
    readonly property var agentNames: ({ claude: "Claude", openai: "OpenAI", local: "Local agent", api: "Anthropic API" })

    ListModel { id: feed }

    Connections {
        target: home.agent
        // A conversation from mobarchy-agent (on start, or one the owner opened).
        function onConversationLoaded(turns) {
            feed.clear()
            let lastAgent = ""
            for (const t of turns) {
                if (t.role === "user" && lastAgent && t.agent !== lastAgent) {
                    feed.append({ who: "note", body: qsTr("Now using %1").arg(home.agentNames[t.agent] ?? t.agent) })
                }
                if (t.role === "user")
                    lastAgent = t.agent
                feed.append({ who: ({ user: "you", assistant: "mobarchy" })[t.role] ?? t.role, body: t.text })
            }
        }
        function onRunningChanged() {
            if (!home.agent.running)
                home.agent.refreshConversations()
        }
        function onEvent(kind, text) {
            if ((kind === "done" && !text) || kind === "code")
                return
            feed.append({ who: kind === "text" ? "mobarchy" : kind, body: text })
        }
    }

    // Talking instead of typing: speech becomes text on the phone, then goes as a message.
    Voice {
        id: voice
        onTranscribed: (text) => {
            ask.text = text
            home.submit()
        }
        onFailed: (reason) => feed.append({ who: "error", body: reason })
    }
    readonly property bool listening: voice.state === Voice.Listening
    readonly property bool transcribing: voice.state === Voice.Transcribing

    function submit() {
        const task = ask.text.trim()
        if (agent.awaitingCode) {  // a pasted login code
            agent.sendCode(task)
            ask.text = ""
            return
        }
        if (!task || agent.running || !chosen)
            return
        feed.append({ who: "you", body: task })
        ask.text = ""
        ask.focus = false
        keyboard.visible = false
        agent.ask(task)
    }
    function choose(id) {
        picking = false
        const a = agentById(id)
        if (a && a.ready && id !== agent.active)
            feed.append({ who: "note", body: qsTr("Now using %1").arg(a.name) })
        agent.choose(id)
    }
    function newConversation() {
        browsing = false
        feed.clear()
        agent.newConversation()
    }

    // --- header: the active agent, and a new conversation ---------------------------------
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        height: 40
        visible: home.chosen

        Rectangle {
            id: chip
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: chipRow.implicitWidth + 28
            height: 36
            radius: 18
            color: chipTap.pressed || home.picking ? Theme.line : Theme.panel
            border.color: Theme.line
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: home.agentIcons[home.agent.active] ?? "✦"
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16 }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: home.current ? home.current.name : ""
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 13; bold: true }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: ""  // nf-fa-chevron_down
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 10 }
                }
            }
            MouseArea {
                id: chipTap
                anchors.fill: parent
                onClicked: {
                    home.agent.refresh()
                    home.picking = !home.picking
                }
            }
        }
        MouseArea {
            anchors { right: newButton.left; verticalCenter: parent.verticalCenter }
            width: 44; height: 40
            onClicked: {
                home.picking = false
                home.agent.refreshConversations()
                home.browsing = !home.browsing
            }
            Text {
                anchors.centerIn: parent
                text: "󰋚"  // history: earlier conversations
                color: home.browsing ? Theme.accent : Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
        }
        MouseArea {
            id: newButton
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: 44; height: 40
            visible: feed.count > 0
            onClicked: home.newConversation()
            Text {
                anchors.centerIn: parent
                text: "󰐕"  // new conversation
                color: Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
        }
    }

    // --- the conversation ------------------------------------------------------------------
    ListView {
        maximumFlickVelocity: Theme.flickVelocity
        boundsBehavior: Theme.boundsBehavior
        flickDeceleration: Theme.flickDeceleration
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
        id: list
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: composer.top
                  leftMargin: 16; rightMargin: 16; topMargin: 8; bottomMargin: 16 }
        spacing: 10
        clip: true
        model: feed
        visible: home.chosen
        // No current item: otherwise the view animates its (invisible) highlight towards
        // it and renders at full frame rate for seconds.
        currentIndex: -1
        highlightFollowsCurrentItem: false
        onCountChanged: Qt.callLater(positionViewAtEnd)

        interactive: !home.dragging
        delegate: Rectangle {
            id: card
            required property string who
            required property string body
            // tool calls and notes are shown short; the rest can be selected and copied
            readonly property bool selectable: who !== "url" && who !== "tool" && who !== "note"
            readonly property string prefix: ({ you: "❯ ", mobarchy: "✦ ", done: "✓ ", error: "✗ " }[who] ?? "")
            width: ListView.view.width
            height: who === "url" ? 52
                  : (selectable ? msg.implicitHeight : short.implicitHeight) + (selectable ? 24 : 8)
            radius: 12
            color: who === "you" ? Theme.panel : "transparent"
            border.width: who === "done" || who === "error" || who === "url" ? 1 : 0
            border.color: who === "error" ? Theme.red : who === "url" ? Theme.accent : Theme.green

            Text {
                id: short
                visible: card.who === "tool" || card.who === "note"
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 12; rightMargin: 12 }
                wrapMode: Text.Wrap
                horizontalAlignment: card.who === "note" ? Text.AlignHCenter : Text.AlignLeft
                // Tool calls can be long (a whole shell script): two lines are enough here.
                maximumLineCount: card.who === "tool" ? 2 : 10000
                elide: Text.ElideRight
                text: (card.who === "tool" ? "→ " : "") + card.body
                color: Theme.muted
                font { family: Theme.font; pixelSize: 12 }
            }

            // Selectable as on a phone: hold to select a word, drag the handles; the shell's
            // Copy button offers what is selected.
            TextEdit {
                id: msg
                visible: card.selectable
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 12; rightMargin: 12 }
                readOnly: true
                selectByMouse: false
                activeFocusOnPress: false   // no focus: the keyboard stays down
                persistentSelection: true
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                text: card.prefix + card.body
                color: ({ mobarchy: Theme.accent, done: Theme.green, error: Theme.red }[card.who] ?? Theme.fg)
                selectionColor: Theme.accent
                selectedTextColor: Theme.bgDark
                font { family: Theme.font; pixelSize: 15 }
                onSelectedTextChanged: if (!home.dragging && selectedText !== "") home.copyOffered(selectedText)

                // on the text itself: a handler here sees the press before the text does
                TapHandler {
                    onLongPressed: {
                        msg.cursorPosition = msg.positionAt(point.position.x, point.position.y)
                        msg.selectWord()
                    }
                    onTapped: msg.deselect()
                }
            }
            readonly property bool selecting: msg.selectedText !== ""


            // the selection's handles
            // a thin bar along the line with a dot (as phones draw them); a larger area takes the finger
            component Handle: Item {
                id: handle
                required property bool start
                readonly property rect at: msg.positionToRectangle(start ? msg.selectionStart : msg.selectionEnd)
                visible: card.selecting
                width: 36; height: at.height + 30
                x: msg.x + at.x - width / 2
                y: msg.y + at.y - (handle.start ? 12 : 0)
                z: 5
                Rectangle {     // the bar, the line's height
                    x: handle.width / 2 - 1
                    y: handle.start ? 12 : 0
                    width: 2; height: handle.at.height
                    color: Theme.accent
                }
                Rectangle {     // the dot: above the start, below the end
                    width: 12; height: 12; radius: 6
                    x: handle.width / 2 - 6
                    y: handle.start ? 2 : handle.at.height - 2
                    color: Theme.accent
                }
                DragHandler {
                    target: null
                    onActiveChanged: {
                        home.dragging = active
                        if (!active && msg.selectedText !== "")
                            home.copyOffered(msg.selectedText)
                    }
                    onCentroidChanged: {
                        if (!active) return
                        // the finger sits on the dot: aim at the middle of the line
                        const dy = handle.start ? 12 + handle.at.height / 2 : handle.at.height / 2
                        const p = handle.mapToItem(msg, centroid.position.x,
                                                   centroid.position.y - centroid.pressPosition.y + dy)
                        const pos = msg.positionAt(p.x, p.y)
                        if (handle.start)
                            msg.select(Math.min(pos, msg.selectionEnd - 1), msg.selectionEnd)
                        else
                            msg.select(msg.selectionStart, Math.max(pos, msg.selectionStart + 1))
                    }
                }
            }
            Handle { start: true }
            Handle { start: false }

            // select the whole message
            Rectangle {
                visible: card.selecting
                anchors { right: parent.right; top: parent.top; margins: 4 }
                width: allText.implicitWidth + 16; height: 24; radius: 12
                color: Theme.bgDark
                border.color: Theme.accent
                z: 6
                Text {
                    id: allText
                    anchors.centerIn: parent
                    text: qsTr("Alt")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 12 }
                }
                TapHandler { onTapped: msg.select(card.prefix.length, msg.length) }
            }

            // A link from the agent: the login page by hand (its CLI opens the browser
            // itself, with a login that comes back on its own; this one shows a code to paste).
            MouseArea {
                anchors.fill: parent
                visible: card.who === "url"
                onClicked: home.agent.openUrl(card.body)
                Text {
                    anchors.centerIn: parent
                    text: "󰌹  " + qsTr("Open the login page")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 14 }
                }
            }
        }
    }

    // --- earlier conversations -----------------------------------------------------------------
    Rectangle {
        visible: home.browsing && home.chosen
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: composer.top
                  margins: 16; topMargin: 8 }
        z: 3
        radius: 16
        color: Theme.bg
        border.color: Theme.line

        Text {
            id: historyTitle
            anchors { left: parent.left; top: parent.top; margins: 16 }
            text: qsTr("Conversations")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 16; bold: true }
        }
        Text {
            anchors { left: parent.left; top: historyTitle.bottom; margins: 16 }
            visible: home.agent.conversations.length === 0
            text: qsTr("No earlier conversations yet.")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 13 }
        }
        ListView {
            maximumFlickVelocity: Theme.flickVelocity
            boundsBehavior: Theme.boundsBehavior
            flickDeceleration: Theme.flickDeceleration
            rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
            anchors { left: parent.left; right: parent.right; top: historyTitle.bottom; bottom: parent.bottom
                      margins: 12; topMargin: 12 }
            clip: true
            spacing: 6
            currentIndex: -1  // see the feed above
            highlightFollowsCurrentItem: false
            model: home.agent.conversations
            delegate: Rectangle {
                id: entry
                required property var modelData
                width: ListView.view.width
                height: 56
                radius: 12
                color: entryTap.pressed ? Theme.line : (modelData.current ? Theme.panel : "transparent")
                border.color: modelData.current ? Theme.accent : "transparent"
                MouseArea {
                    id: entryTap
                    anchors.fill: parent
                    onClicked: {
                        home.browsing = false
                        home.agent.openConversation(entry.modelData.id)
                    }
                }
                Column {
                    anchors { left: parent.left; right: parent.right; margins: 14; verticalCenter: parent.verticalCenter }
                    spacing: 3
                    Text {
                        width: parent.width
                        text: entry.modelData.title || qsTr("(empty)")
                        elide: Text.ElideRight
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 14 }
                    }
                    Text {
                        text: entry.modelData.agents.map(a => home.agentIcons[a] ?? a).join(" ") + "   "
                              + new Date(entry.modelData.updated * 1000).toLocaleString(Qt.locale(), "d MMM HH:mm")
                        color: Theme.muted
                        font { family: Theme.font; pixelSize: 11 }
                    }
                }
            }
        }
    }

    // --- choose your agent -------------------------------------------------------------------
    component AgentCard: Rectangle {
        id: agentCard
        required property var info
        readonly property bool usable: info.available
        width: Math.min(parent ? parent.width : 400, 420)
        height: 72
        radius: 16
        opacity: usable ? 1 : 0.45
        color: tap.pressed ? Theme.line : Theme.panel
        border.color: info.id === home.agent.active && info.ready ? Theme.accent : Theme.line
        MouseArea { id: tap; anchors.fill: parent; enabled: agentCard.usable && !home.agent.running
                    onClicked: home.choose(agentCard.info.id) }
        Text {
            id: cardIcon
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            text: home.agentIcons[agentCard.info.id] ?? "✦"
            color: Theme.accent
            font { family: Theme.font; pixelSize: 26 }
        }
        Column {
            anchors { left: cardIcon.right; leftMargin: 18; right: parent.right; rightMargin: 16
                      verticalCenter: parent.verticalCenter }
            spacing: 3
            Text {
                text: agentCard.info.name
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15; bold: true }
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: agentCard.info.ready ? (agentCard.info.detail || qsTr("Ready"))
                    : !agentCard.usable ? agentCard.info.detail
                    : agentCard.info.installed ? qsTr("Tap to log in") : qsTr("Tap to set up")
                color: agentCard.info.ready ? Theme.green : Theme.muted
                font { family: Theme.font; pixelSize: 12 }
            }
        }
        // Log out of the account (to log in as someone else, or to try the login again)
        Text {
            visible: agentCard.info.ready && (agentCard.info.id === "claude" || agentCard.info.id === "openai")
            anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
            text: qsTr("Log out")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 12; underline: true }
            MouseArea {
                anchors { fill: parent; margins: -12 }
                enabled: !home.agent.running
                onClicked: home.agent.logout(agentCard.info.id)
            }
        }
    }

    // First run: in the middle of the screen. Later: as a list under the chip.
    Column {
        id: chooser
        visible: !home.chosen || home.picking
        anchors.horizontalCenter: parent.horizontalCenter
        y: home.chosen ? header.y + header.height + 8 : Math.max(24, (parent.height - height) / 2 - 40)
        width: Math.min(parent.width - 32, 420)
        spacing: 12
        z: 2

        Text {
            visible: !home.chosen
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Choose your agent")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 22; bold: true }
            bottomPadding: 8
        }
        Repeater {
            // The three the owner chooses between; the API-key agent is for later (settings).
            model: home.agent.agents.filter(a => a.id !== "api")
            delegate: AgentCard {
                required property var modelData
                info: modelData
                width: chooser.width
            }
        }
        // While setting up (installing, logging in): what is happening.
        Repeater {
            model: home.chosen ? null : feed
            delegate: Text {
                required property string who
                required property string body
                width: chooser.width
                visible: who !== "url"
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                text: body
                color: who === "error" ? Theme.red : who === "done" ? Theme.green : Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
        }
        Text {
            visible: !home.chosen && feed.count > 0 && feed.get(feed.count - 1).who === "url"
            anchors.horizontalCenter: parent.horizontalCenter
            text: "󰌹  " + qsTr("Open the login page")
            color: Theme.accent
            font { family: Theme.font; pixelSize: 14 }
            MouseArea { anchors { fill: parent; margins: -10 }
                        onClicked: home.agent.openUrl(feed.get(feed.count - 1).body) }
        }
    }

    // --- ask ---------------------------------------------------------------------------------
    Rectangle {
        id: composer
        visible: home.chosen || home.agent.awaitingCode
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        height: 48
        radius: 24
        color: Theme.panel
        border.color: ask.activeFocus ? Theme.accent : Theme.line

        // Cancel talking.
        MouseArea {
            id: cancelVoice
            visible: home.listening || home.transcribing
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: visible ? 48 : 0
            onClicked: voice.cancel()
            Text {
                anchors.centerIn: parent
                text: "󰅖"
                color: Theme.muted
                font { family: Theme.font; pixelSize: 18 }
            }
        }
        TextInput {
            id: ask
            anchors { left: cancelVoice.right; right: action.left; leftMargin: cancelVoice.visible ? 0 : 20
                      rightMargin: 8; verticalCenter: parent.verticalCenter }
            visible: !home.listening && !home.transcribing
            clip: true
            color: Theme.fg
            selectionColor: Theme.accent
            font { family: Theme.font; pixelSize: 15 }
            onAccepted: home.submit()
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !ask.text && !ask.activeFocus
                text: home.agent.awaitingCode ? qsTr("Paste the code from the login page …")
                    : home.agent.running ? qsTr("Working …")
                    : qsTr("Ask %1 …").arg(home.current ? home.current.name : "Mobarchy")
                color: Theme.muted
                font: ask.font
            }
        }
        Text {
            anchors { left: cancelVoice.right; right: action.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            visible: home.listening || home.transcribing
            text: home.listening ? qsTr("Listening … %1 s  ·  tap 󰒊 to send").arg(voice.seconds)
                                 : qsTr("Writing it down …")
            elide: Text.ElideRight
            color: home.listening ? Theme.fg : Theme.muted
            font { family: Theme.font; pixelSize: 15 }
        }
        MouseArea {
            id: action
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 56
            // Stop the agent; send what's said or typed; or, with nothing typed, start talking.
            readonly property string mode: home.agent.running && !home.agent.awaitingCode ? "stop"
                : home.listening ? "finish"
                : home.transcribing ? "wait"
                : ask.text || home.agent.awaitingCode ? "send" : "talk"
            onClicked: {
                switch (mode) {
                case "stop": home.agent.stop(); break
                case "finish": voice.finish(); break
                case "send": home.submit(); break
                case "talk":
                    ask.focus = false
                    home.keyboard.visible = false
                    voice.start()
                    break
                }
            }
            // Holding the microphone: talk with Frigg, live (ADR 0026); a tap dictates.
            onPressAndHold: if (mode === "talk") {
                ask.focus = false
                home.keyboard.visible = false
                home.talk.start()
            }
            Text {
                anchors.centerIn: parent
                text: ({ stop: "󰓛", finish: "󰒊", wait: "󰔟", send: "󰒊", talk: "󰍬" })[action.mode]
                color: action.mode === "stop" ? Theme.red
                     : action.mode === "finish" || action.mode === "send" ? Theme.accent : Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
            // Listening: a pulsing ring.
            Rectangle {
                anchors.centerIn: parent
                width: 36; height: 36; radius: 18
                color: "transparent"
                border.color: Theme.red
                border.width: 2
                visible: home.listening
                SequentialAnimation on opacity {
                    running: home.listening
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.2; duration: 700 }
                    NumberAnimation { from: 0.2; to: 1; duration: 700 }
                }
            }
        }
    }
}
