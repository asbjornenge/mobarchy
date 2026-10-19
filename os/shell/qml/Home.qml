pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Mobarchy.Shell

// Home: widgets (HomeBoard), and the agent one tap away: the Frigg card opens the
// conversation, live (FriggStage: her orb, what she hears and says) or in text (a feed of what
// the agent says and does, and the field to ask it something; her orb small in the corner).
// "Aa" goes from live to text, the small orb back to live; the conversations (earlier ones, a
// new one) are at the top in both. Swiped down (or home) it closes, and a live conversation
// ends.
// A normal window, not a layer surface: text input (and the on-screen keyboard) only
// works there on Hyprland (ADR 0006).
//
// Which agent answers is the owner's choice (ADR 0010): "Choose your agent" until one is
// set up, then a chip at the top shows the active one and switches between them.
Window {
    id: home

    required property Agent agent
    required property Talk talk
    required property FriggSettings friggSettings
    required property Keyboard keyboard
    required property MediaPlayers media
    required property Alarms alarms
    required property Telephony telephony
    required property SystemStatus system
    required property AppModel apps
    required property ToplevelModel toplevels
    // Text selected in the conversation: the shell's Copy button offers it.
    signal copyOffered(string text)
    property bool dragging: false   // a selection handle is being dragged

    title: "Mobarchy"
    color: Theme.bg
    Component.onCompleted: show()

    // The theme's wallpaper (ADR 0025): behind the widgets dimmed a little, behind the
    // conversation (text to read) more.
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
        opacity: 0.6 + 0.3 * home.open
    }

    // --- the conversation: 0 = closed (the widgets), 1 = open --------------------------------
    // (Until an agent is chosen, it is where one is chosen.)
    property real open: 0
    property string mode: "live"    // "live" (talking with Frigg) or "text"
    // Frigg is shown here, not in the overlay: open (from the first frame), live, in front
    readonly property bool hostsTalk: open > 0 && mode === "live" && active
    property bool sheetDragging: false
    readonly property real chatOpacity: Math.max(0, (open - 0.35) / 0.65)
    Behavior on open {
        enabled: !home.sheetDragging
        NumberAnimation { duration: 320; easing.type: Easing.OutCubic }
    }
    function expand() { open = 1 }
    function openFrigg(how) {
        picking = false
        browsing = false
        mode = chosen ? how : "text"
        open = 1
        if (mode === "live" && !talk.talking)
            talk.start()
    }
    function toText() {
        if (talk.talking)
            talk.stop()
        mode = "text"
    }
    function toLive() {
        ask.focus = false
        keyboard.visible = false
        browsing = false
        picking = false
        mode = "live"
        if (!talk.talking)
            talk.start()
    }
    function collapse() {
        if (open > 0 && mode === "live" && talk.talking)
            talk.stop()
        picking = false
        browsing = false
        ask.focus = false
        keyboard.visible = false
        open = 0
    }
    // The finger moves the sheet; let go, it settles by how far it got or how it was thrown.
    function settle(velocity) {
        sheetDragging = false
        if (velocity < -600) expand()
        else if (velocity > 600) collapse()
        else if (open > 0.5) expand()
        else collapse()
    }

    HomeConfig { id: homeConfig }

    HomeBoard {
        id: board
        anchors.fill: parent
        visible: home.open < 1
        opacity: 1 - home.open
        scale: 1 - 0.06 * home.open
        interactive: home.open === 0
        config: homeConfig
        media: home.media
        alarms: home.alarms
        telephony: home.telephony
        system: home.system
        apps: home.apps
        toplevels: home.toplevels
        talk: home.talk
        bottomInset: 8
        onFriggAsked: home.openFrigg("live")
    }

    // The conversation's ground, over the widgets (which fade and recede).
    Rectangle {
        id: sheet
        readonly property real topGap: 8
        visible: home.open > 0
        anchors.fill: parent
        anchors.topMargin: topGap + 40 * (1 - home.open)
        opacity: home.open
        radius: 28
        color: Qt.alpha(Theme.bgDark, 0.95)
        border.color: Theme.line

        // the grip: drag (or tap) to put the conversation away
        Item {
            id: grip
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 28
            visible: home.open > 0.5
            Rectangle {
                anchors.centerIn: parent
                width: 40; height: 5; radius: 2.5
                color: Theme.muted
                opacity: 0.6
            }
            TapHandler { onTapped: home.collapse() }
        }
    }
    // Pulling the conversation down by its top (the grip and the header).
    Item {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: sheet.topGap + grip.height + 4
        visible: home.open > 0.5
        z: 4
        DragHandler {
            target: null
            yAxis.enabled: true
            xAxis.enabled: false
            onActiveChanged: if (active) home.sheetDragging = true
                             else home.settle(centroid.velocity.y)
            onTranslationChanged: if (active) home.open = Math.max(0, Math.min(1, 1 - translation.y / (home.height * 0.7)))
        }
        TapHandler { onTapped: home.collapse() }
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
        expand()
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

    // --- Frigg: live, large; in text, small in the corner -----------------------------------
    FriggStage {
        id: stage
        anchors { fill: parent; topMargin: sheet.topGap + grip.height }
        visible: home.chosen && home.open > 0
        opacity: home.chatOpacity
        talk: home.talk
        settings: home.friggSettings
        small: home.mode === "text"
        topInset: 60
        smallSize: 40
        smallRight: 16
        smallTop: 0
        onCloseAsked: home.collapse()
        onTextAsked: home.toText()
        onLiveAsked: home.toLive()
    }

    // --- header: the active agent, and a new conversation ---------------------------------
    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16
                  topMargin: sheet.topGap + grip.height
                  rightMargin: home.mode === "text" ? 16 + 40 + 8 : 16 }
        height: 40
        visible: home.chosen && home.open > 0
        opacity: home.chatOpacity

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
        visible: home.chosen && home.open > 0 && home.mode === "text"
        opacity: home.chatOpacity
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
        Connections {
            target: home
            function onActiveChanged() { if (home.active) list.keepAtEnd() }
        }

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
        visible: home.browsing && home.chosen && home.open > 0
        opacity: home.chatOpacity
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
        visible: (!home.chosen || home.picking) && home.open > 0
        opacity: home.chatOpacity
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
        // Claude's model: for the chat, and for Frigg (talking)
        Repeater {
            model: home.chosen && home.agent.active === "claude" && home.agent.modelChoices.length > 0
                   ? [{which: "chat", label: qsTr("Model"), choices: [{id: "default", name: qsTr("Default")}].concat(home.agent.modelChoices)},
                      {which: "frigg", label: qsTr("Frigg"), choices: home.agent.modelChoices}]
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
                            readonly property string current: home.agent.models[modelRow.modelData.which] || "default"
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
                                onClicked: home.agent.setModel(modelRow.modelData.which, choice.modelData.id)
                            }
                        }
                    }
                }
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
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        height: 52
        radius: 26
        z: 5
        // in text (or while logging in: the code to paste)
        visible: home.open > 0 && (home.chosen && home.mode === "text" || home.agent.awaitingCode)
        opacity: home.chatOpacity
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
            enabled: home.chosen || home.agent.awaitingCode
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !ask.text && !ask.activeFocus
                text: !home.chosen && !home.agent.awaitingCode ? qsTr("Choose your agent …")
                    : home.agent.awaitingCode ? qsTr("Paste the code from the login page …")
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
            onPressAndHold: if (mode === "talk") home.toLive()
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
