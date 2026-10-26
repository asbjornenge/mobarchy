pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Frigg: the conversation with the agent, over everything (TalkOverlay; ADR 0026, 0037).
// Live: her orb large (FriggStage), what she hears and says. In text: a feed of what the agent
// says and does and the field to ask it something, her orb small in the corner. "Aa" goes from
// live to text, the small orb back to live; the conversations (earlier ones, a new one) are at
// the top in both. Swiped down (or ✕) it closes, and a live conversation ends.
//
// The field is typed into by the shell's own keyboard (Keyboard's capture, as the apps page's
// search): Hyprland gives no text input to a layer surface (ADR 0006). A hardware keyboard
// types into it too: in text the overlay takes the keyboard (TalkOverlay), and its keys come
// here (keysWanted).
//
// Which agent answers is the owner's choice (ADR 0010): "Choose your agent" until one is
// set up, then a chip at the top shows the active one and switches between them.
Item {
    id: view

    required property Agent agent
    required property Talk talk
    required property FriggSettings settings
    required property Keyboard keyboard
    property bool locked: false     // on the lock screen: Frigg live, nothing of the conversations
    // Text selected in the conversation: the shell's Copy button offers it.
    signal copyOffered(string text)
    signal closed()
    property bool dragging: false   // a selection handle is being dragged

    // --- open: 0 closed, 1 open ---------------------------------------------------------------
    property real open: 0
    property string mode: "live"    // "live" (talking with Frigg) or "text"
    readonly property real chatOpacity: open
    readonly property real topGap: 36   // the grip
    Behavior on open { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    function openWith(how) {
        picking = false
        browsing = false
        mode = chosen && !(locked && how === "text") ? how : (locked ? "live" : "text")
        open = 1
        if (mode === "live" && !talk.talking)
            talk.start()
    }
    // (in text she sleeps: the microphone open, nothing heard or said, the conversation
    // kept; back to live she is there at once)
    function toText() {
        talk.pause()
        mode = "text"
    }
    function toLive() {
        endTyping()
        browsing = false
        picking = false
        mode = "live"
        if (!talk.talking)
            talk.start()
    }
    function collapse() {
        if (talk.active)
            talk.stop()
        picking = false
        browsing = false
        endTyping()
        open = 0
        closed()
    }

    // --- typing (Keyboard's capture) -------------------------------------------------------
    property bool typingHere: false
    readonly property bool typing: typingHere && keyboard.capturing
    readonly property string typed: typing ? keyboard.captured : ""
    function startTyping() {
        typingHere = true
        keyboard.startCapture()
        keyboard.visible = true
    }
    function endTyping() {
        if (!typingHere)
            return
        typingHere = false
        keyboard.stopCapture()
        keyboard.visible = false
    }
    // A hardware keyboard: in text, typing goes into the field (no on-screen keyboard for it),
    // Return sends, Backspace rubs out; Escape ends the typing, then closes.
    // (only while fully open: the keyboard let go of as soon as it starts to close, before the
    // overlay's surface goes; Hyprland crashed refocusing when a surface went with it)
    readonly property bool keysWanted: open >= 0.999 && mode === "text" && !locked
    function key(event) {
        if (event.key === Qt.Key_Escape) {
            if (typing) endTyping(); else collapse()
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (typing) {
                submit()
                // (on in the conversation: the field is still the one typed into)
                typingHere = true
                keyboard.startCapture()
            }
        } else if (event.key === Qt.Key_Backspace) {
            if (typing) keyboard.backspace()
        } else if (event.text !== "" && event.text.charCodeAt(0) >= 32
                   && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier))) {
            if (!typing) {
                typingHere = true
                keyboard.startCapture()
            }
            keyboard.type(event.text)
        } else {
            return
        }
        event.accepted = true
    }
    Item {
        id: keyCatcher
        focus: view.keysWanted
        Keys.onPressed: (event) => view.key(event)
        // (an answer coming in took it: back, there is nothing else here to type into)
        onActiveFocusChanged: if (!activeFocus && view.keysWanted) Qt.callLater(view.takeKeys)
    }
    function takeKeys() { if (keysWanted) keyCatcher.forceActiveFocus() }
    onKeysWantedChanged: takeKeys()

    Connections {
        target: view.keyboard
        function onCaptureAccepted() { if (view.typing) view.submit() }
        // (the keyboard put away by hand: the typing ends)
        function onVisibleChanged() { if (!view.keyboard.visible) view.endTyping() }
    }

    // the ground: dimmed in live (what is behind shows through), solid for reading
    Rectangle {
        anchors.fill: parent
        color: view.mode === "live" ? Qt.alpha(Theme.bgDark, 0.82) : Qt.alpha(Theme.bgDark, 0.96)
        Behavior on color { ColorAnimation { duration: 260 } }
        MouseArea { anchors.fill: parent }
    }
    // the grip: drag down (or tap) to close
    Item {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: view.topGap
        z: 4
        Rectangle {
            anchors.centerIn: parent
            width: 40; height: 5; radius: 2.5
            color: Theme.muted
            opacity: 0.6
        }
        TapHandler { onTapped: view.collapse() }
        DragHandler {
            target: null
            xAxis.enabled: false
            onActiveChanged: if (!active && (translation.y > 120 || centroid.velocity.y > 600)) view.collapse()
        }
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
        target: view.agent
        // A conversation from mobarchy-agent (on start, or one the owner opened).
        function onConversationLoaded(turns) {
            feed.clear()
            let lastAgent = ""
            for (const t of turns) {
                if (t.role === "user" && lastAgent && t.agent !== lastAgent) {
                    feed.append({ who: "note", body: qsTr("Now using %1").arg(view.agentNames[t.agent] ?? t.agent) })
                }
                if (t.role === "user")
                    lastAgent = t.agent
                feed.append({ who: ({ user: "you", assistant: "mobarchy" })[t.role] ?? t.role, body: t.text })
            }
        }
        function onRunningChanged() {
            if (!view.agent.running)
                view.agent.refreshConversations()
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
        onTranscribed: (text) => view.submit(text)
        onFailed: (reason) => feed.append({ who: "error", body: reason })
    }
    readonly property bool listening: voice.state === Voice.Listening
    readonly property bool transcribing: voice.state === Voice.Transcribing

    // what is typed (or said: given) goes to the agent
    function submit(given) {
        const task = (given ?? typed).trim()
        if (agent.awaitingCode) {  // a pasted login code
            agent.sendCode(task)
            endTyping()
            return
        }
        if (!task || agent.running || !chosen)
            return
        feed.append({ who: "you", body: task })
        endTyping()
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

    // --- Frigg: live, large; in text, small in the corner -----------------------------------
    FriggStage {
        id: stage
        anchors { fill: parent; topMargin: view.topGap }
        visible: view.chosen && view.open > 0
        opacity: view.chatOpacity
        talk: view.talk
        settings: view.settings
        locked: view.locked
        offerText: !view.locked
        shown: view.open > 0
        small: view.mode === "text"
        topInset: 60
        smallSize: 40
        smallRight: 16
        smallTop: 0
        onCloseAsked: view.collapse()
        onTextAsked: view.toText()
        onLiveAsked: view.toLive()
    }

    // --- header: the active agent, and a new conversation ---------------------------------
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16
                  topMargin: view.topGap
                  rightMargin: view.mode === "text" ? 16 + 40 + 8 : 16 }
        height: 40
        visible: view.chosen && view.open > 0 && !view.locked
        opacity: view.chatOpacity

        Rectangle {
            id: chip
            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
            width: chipRow.implicitWidth + 28
            height: 36
            radius: 18
            color: chipTap.pressed || view.picking ? Theme.line : Theme.panel
            border.color: Theme.line
            Row {
                id: chipRow
                anchors.centerIn: parent
                spacing: 8
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: view.agentIcons[view.agent.active] ?? "✦"
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16 }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: view.current ? view.current.name : ""
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
                    view.agent.refresh()
                    view.picking = !view.picking
                }
            }
        }
        MouseArea {
            anchors { right: newButton.left; verticalCenter: parent.verticalCenter }
            width: 44; height: 40
            onClicked: {
                view.picking = false
                view.agent.refreshConversations()
                view.browsing = !view.browsing
            }
            Text {
                anchors.centerIn: parent
                text: "󰋚"  // history: earlier conversations
                color: view.browsing ? Theme.accent : Theme.muted
                font { family: Theme.font; pixelSize: 20 }
            }
        }
        MouseArea {
            id: newButton
            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
            width: 44; height: 40
            visible: feed.count > 0
            onClicked: view.newConversation()
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
        visible: view.chosen && view.open > 0 && view.mode === "text"
        opacity: view.chatOpacity
        // No current item: otherwise the view animates its (invisible) highlight towards
        // it and renders at full frame rate for seconds.
        currentIndex: -1
        highlightFollowsCurrentItem: false
        // At the end it stays at the end: as messages come and grow (an answer streaming in),
        // and when the view changes size (the keyboard, a fold, the window back in front).
        // Its items are of different heights and made as they come into sight, so the view
        // only estimates the whole height: a size change otherwise left it at the same
        // offset in a changed estimate, somewhere far up. Scrolled up by the owner, it stays.
        property bool atEnd: true
        onMovementEnded: atEnd = atYEnd
        onFlickEnded: atEnd = atYEnd
        function keepAtEnd() {
            if (atEnd)
                Qt.callLater(positionViewAtEnd)
        }
        onCountChanged: { atEnd = true; keepAtEnd() }
        onContentHeightChanged: keepAtEnd()
        onHeightChanged: keepAtEnd()
        onWidthChanged: keepAtEnd()
        onVisibleChanged: if (visible) keepAtEnd()

        interactive: !view.dragging
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
                onSelectedTextChanged: if (!view.dragging && selectedText !== "") view.copyOffered(selectedText)

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
                        view.dragging = active
                        if (!active && msg.selectedText !== "")
                            view.copyOffered(msg.selectedText)
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
                onClicked: view.agent.openUrl(card.body)
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
        visible: view.browsing && view.chosen && view.open > 0
        opacity: view.chatOpacity
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
            visible: view.agent.conversations.length === 0
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
            model: view.agent.conversations
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
                        view.browsing = false
                        view.agent.openConversation(entry.modelData.id)
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
                        text: entry.modelData.agents.map(a => view.agentIcons[a] ?? a).join(" ") + "   "
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
        border.color: info.id === view.agent.active && info.ready ? Theme.accent : Theme.line
        MouseArea { id: tap; anchors.fill: parent; enabled: agentCard.usable && !view.agent.running
                    onClicked: view.choose(agentCard.info.id) }
        Text {
            id: cardIcon
            anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
            text: view.agentIcons[agentCard.info.id] ?? "✦"
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
                enabled: !view.agent.running
                onClicked: view.agent.logout(agentCard.info.id)
            }
        }
    }

    // First run: in the middle of the screen. Later: as a list under the chip.
    Column {
        id: chooser
        visible: (!view.chosen || view.picking) && view.open > 0
        opacity: view.chatOpacity
        anchors.horizontalCenter: parent.horizontalCenter
        y: view.chosen ? header.y + header.height + 8 : Math.max(24, (parent.height - height) / 2 - 40)
        width: Math.min(parent.width - 32, 420)
        spacing: 12
        z: 2

        Text {
            visible: !view.chosen
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Choose your agent")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 22; bold: true }
            bottomPadding: 8
        }
        Repeater {
            // The three the owner chooses between; the API-key agent is for later (settings).
            model: view.agent.agents.filter(a => a.id !== "api")
            delegate: AgentCard {
                required property var modelData
                info: modelData
                width: chooser.width
            }
        }
        // Claude's model: for the chat, and for Frigg (talking)
        Repeater {
            model: view.chosen && view.agent.active === "claude" && view.agent.modelChoices.length > 0
                   ? [{which: "chat", label: qsTr("Model"), choices: [{id: "default", name: qsTr("Default")}].concat(view.agent.modelChoices)},
                      {which: "frigg", label: qsTr("Frigg"), choices: view.agent.modelChoices}]
                   : []
            delegate: Column {
                id: modelRow
                required property var modelData
                width: chooser.width
                spacing: 6
                Text {
                    text: modelRow.modelData.label
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 12 }
                }
                Row {
                    spacing: 6
                    Repeater {
                        model: modelRow.modelData.choices
                        delegate: Rectangle {
                            id: choice
                            required property var modelData
                            readonly property string current: view.agent.models[modelRow.modelData.which] || "default"
                            readonly property bool on: current === modelData.id
                            width: (chooser.width - 6 * (modelRow.modelData.choices.length - 1)) / modelRow.modelData.choices.length
                            height: 36
                            radius: 18
                            color: on ? Theme.accent : (choiceTap.pressed ? Theme.line : Theme.panel)
                            border.color: Theme.line
                            Text {
                                anchors.centerIn: parent
                                text: choice.modelData.name
                                color: choice.on ? Theme.bg : Theme.fg
                                font { family: Theme.font; pixelSize: 13; bold: choice.on }
                            }
                            MouseArea {
                                id: choiceTap
                                anchors.fill: parent
                                onClicked: view.agent.setModel(modelRow.modelData.which, choice.modelData.id)
                            }
                        }
                    }
                }
            }
        }
        // While setting up (installing, logging in): what is happening.
        Repeater {
            model: view.chosen ? null : feed
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
            visible: !view.chosen && feed.count > 0 && feed.get(feed.count - 1).who === "url"
            anchors.horizontalCenter: parent.horizontalCenter
            text: "󰌹  " + qsTr("Open the login page")
            color: Theme.accent
            font { family: Theme.font; pixelSize: 14 }
            MouseArea { anchors { fill: parent; margins: -10 }
                        onClicked: view.agent.openUrl(feed.get(feed.count - 1).body) }
        }
    }

    // --- ask ---------------------------------------------------------------------------------
    Rectangle {
        id: composer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        height: 52
        radius: 26
        z: 5
        // in text (or while logging in: the code to paste)
        visible: view.open > 0 && !view.locked && (view.chosen && view.mode === "text" || view.agent.awaitingCode)
        opacity: view.chatOpacity
        color: Theme.panel
        border.color: view.typing ? Theme.accent : Theme.line

        // Cancel talking.
        MouseArea {
            id: cancelVoice
            visible: view.listening || view.transcribing
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
        // what is typed (the shell's keyboard types into the shell itself: Keyboard's capture)
        Item {
            id: field
            anchors { left: cancelVoice.right; right: action.left; leftMargin: cancelVoice.visible ? 0 : 20
                      rightMargin: 8; top: parent.top; bottom: parent.bottom }
            visible: !view.listening && !view.transcribing
            clip: true
            Text {
                id: typedText
                anchors.verticalCenter: parent.verticalCenter
                // (the end of it shows, as it is typed)
                x: Math.min(0, field.width - width - 4)
                text: view.typed
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
            }
            Rectangle {     // the cursor, at the end
                visible: view.typing
                x: typedText.x + typedText.width + 1
                anchors.verticalCenter: parent.verticalCenter
                width: 2; height: 20
                color: Theme.accent
                SequentialAnimation on opacity {
                    running: view.typing
                    loops: Animation.Infinite
                    NumberAnimation { to: 0; duration: 500; easing.type: Easing.InOutQuad }
                    NumberAnimation { to: 1; duration: 500; easing.type: Easing.InOutQuad }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: view.typed === "" && !view.typing
                text: !view.chosen && !view.agent.awaitingCode ? qsTr("Choose your agent …")
                    : view.agent.awaitingCode ? qsTr("Paste the code from the login page …")
                    : view.agent.running ? qsTr("Working …")
                    : qsTr("Ask %1 …").arg(view.current ? view.current.name : "Mobarchy")
                color: Theme.muted
                font: typedText.font
            }
            TapHandler {
                enabled: view.chosen || view.agent.awaitingCode
                onTapped: view.startTyping()
            }
        }
        Text {
            anchors { left: cancelVoice.right; right: action.left; rightMargin: 8; verticalCenter: parent.verticalCenter }
            visible: view.listening || view.transcribing
            text: view.listening ? qsTr("Listening … %1 s  ·  tap 󰒊 to send").arg(voice.seconds)
                                 : qsTr("Writing it down …")
            elide: Text.ElideRight
            color: view.listening ? Theme.fg : Theme.muted
            font { family: Theme.font; pixelSize: 15 }
        }
        MouseArea {
            id: action
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 56
            // Stop the agent; send what's said or typed; or, with nothing typed, start talking.
            readonly property string mode: view.agent.running && !view.agent.awaitingCode ? "stop"
                : view.listening ? "finish"
                : view.transcribing ? "wait"
                : view.typed || view.agent.awaitingCode ? "send" : "talk"
            onClicked: {
                switch (mode) {
                case "stop": view.agent.stop(); break
                case "finish": voice.finish(); break
                case "send": view.submit(); break
                case "talk":
                    view.endTyping()
                    voice.start()
                    break
                }
            }
            // Holding the microphone: talk with Frigg, live (ADR 0026); a tap dictates.
            onPressAndHold: if (mode === "talk") view.toLive()
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
                visible: view.listening
                SequentialAnimation on opacity {
                    running: view.listening
                    loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.2; duration: 700 }
                    NumberAnimation { from: 0.2; to: 1; duration: 700 }
                }
            }
        }
    }
}
