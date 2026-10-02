pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Mobarchy.Shell

// Home: the agent. A feed of what it says and does, and a field to ask it something.
// A normal window, not a layer surface: text input (and the on-screen keyboard) only
// works there on Hyprland (ADR 0006).
Window {
    id: home

    required property Agent agent
    required property Keyboard keyboard

    title: "Mobarchy"
    color: Theme.bg
    Component.onCompleted: show()

    ListModel {
        id: feed
        ListElement { who: "mobarchy"; body: qsTr("Hi. What should I do?") }
    }

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
        if (!task || agent.running)
            return
        feed.append({ who: "you", body: task })
        ask.text = ""
        ask.focus = false
        keyboard.visible = false
        agent.ask(task)
    }

    ListView {
        id: list
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: composer.top
                  margins: 16 }
        spacing: 10
        clip: true
        model: feed
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
            height: msg.implicitHeight + (who === "tool" ? 8 : 24)
            radius: 12
            color: who === "you" ? Theme.panel : "transparent"
            border.width: who === "done" || who === "error" ? 1 : 0
            border.color: who === "error" ? Theme.red : Theme.green

            Text {
                id: msg
                anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                          leftMargin: 12; rightMargin: 12 }
                wrapMode: Text.Wrap
                text: ({ you: "❯ ", mobarchy: "✦ ", tool: "→ ", done: "✓ ", error: "✗ " }[card.who] ?? "")
                      + card.body
                color: ({ mobarchy: Theme.accent, tool: Theme.muted, done: Theme.green,
                          error: Theme.red }[card.who] ?? Theme.fg)
                font { family: Theme.font; pixelSize: card.who === "tool" ? 12 : 15 }
            }
        }
    }

    Rectangle {
        id: composer
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
                text: home.agent.running ? qsTr("Working …") : qsTr("Ask Mobarchy …")
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
