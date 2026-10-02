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
    required property Keyboard keyboard

    title: "Mobarchy"
    color: Theme.bg
    Component.onCompleted: show()

    readonly property var current: agentById(agent.active)
    readonly property bool chosen: current !== undefined && current.ready
    property bool picking: false

    function agentById(id) {
        return agent.agents.find(a => a.id === id)
    }
    readonly property var agentIcons: ({ claude: "󰚩", openai: "󰧑", local: "󰍹", api: "󰌆" })

    ListModel { id: feed }

    Connections {
        target: home.agent
        function onEvent(kind, text) {
            if (kind === "done" && !text)
                return
            feed.append({ who: kind === "text" ? "mobarchy" : kind, body: text })
        }
    }

    function submit() {
        const task = ask.text.trim()
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

        delegate: Rectangle {
            id: card
            required property string who
            required property string body
            width: ListView.view.width
            height: who === "url" ? 52 : msg.implicitHeight + (who === "tool" || who === "note" ? 8 : 24)
            radius: 12
            color: who === "you" ? Theme.panel : "transparent"
            border.width: who === "done" || who === "error" || who === "url" ? 1 : 0
            border.color: who === "error" ? Theme.red : who === "url" ? Theme.accent : Theme.green

            Text {
                id: msg
                visible: card.who !== "url"
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 12; rightMargin: 12 }
                wrapMode: Text.Wrap
                horizontalAlignment: card.who === "note" ? Text.AlignHCenter : Text.AlignLeft
                text: ({ you: "❯ ", mobarchy: "✦ ", tool: "→ ", done: "✓ ", error: "✗ " }[card.who] ?? "")
                      + card.body
                color: ({ mobarchy: Theme.accent, tool: Theme.muted, done: Theme.green,
                          error: Theme.red, note: Theme.muted }[card.who] ?? Theme.fg)
                font { family: Theme.font; pixelSize: card.who === "tool" || card.who === "note" ? 12 : 15 }
            }
            // A link from the agent (e.g. the login page if the browser did not open).
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
        visible: home.chosen
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
        height: 48
        radius: 24
        color: Theme.panel
        border.color: ask.activeFocus ? Theme.accent : Theme.line

        TextInput {
            id: ask
            anchors { left: parent.left; right: action.left; leftMargin: 20; rightMargin: 8
                      verticalCenter: parent.verticalCenter }
            clip: true
            color: Theme.fg
            selectionColor: Theme.accent
            font { family: Theme.font; pixelSize: 15 }
            onAccepted: home.submit()
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: !ask.text && !ask.activeFocus
                text: home.agent.running ? qsTr("Working …")
                    : qsTr("Ask %1 …").arg(home.current ? home.current.name : "Mobarchy")
                color: Theme.muted
                font: ask.font
            }
        }
        MouseArea {
            id: action
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 56
            onClicked: home.agent.running ? home.agent.stop() : home.submit()
            Text {
                anchors.centerIn: parent
                text: home.agent.running ? "󰓛" : "󰒊"
                color: home.agent.running ? Theme.red : (ask.text ? Theme.accent : Theme.muted)
                font { family: Theme.font; pixelSize: 20 }
            }
        }
    }
}
