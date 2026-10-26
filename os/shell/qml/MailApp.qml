pragma ComponentBehavior: Bound

import QtQuick
import QtWebEngine
import Mobarchy.Shell

// Mail (ADR 0038), after Spark: the inbox in three (people, notifications, newsletters), a
// swipe right archives and a swipe left snoozes (back at the top when its time comes); a tap
// reads, with reply, forward, archive, snooze and delete; "+" writes one. Pulled down, it
// syncs. The accounts' mail through MailData (mobarchy-mail). Its own window
// (mobarchy-shell --mail).
Window {
    id: win

    visible: true
    title: qsTr("Mail")
    color: Theme.bg

    // (a tick as a swipe passes a mark)
    Haptics { id: haptics }

    // the message's HTML, when it has one; remote images only when asked for
    readonly property bool html: !!(win.message && win.message.html)
    readonly property bool remoteImages: html && /(src|background)\s*=\s*["']?https?:/i.test(win.message.html)
    property bool showImages: false
    onMessageChanged: showImages = false
    function htmlPage() {
        const csp = showImages ? "default-src 'none'; img-src https: http: data:; style-src 'unsafe-inline' https: http:; font-src https: http: data:"
                               : "default-src 'none'; img-src data:; style-src 'unsafe-inline'"
        const head = '<meta http-equiv="Content-Security-Policy" content="' + csp + '">'
                   + '<meta name="viewport" content="width=device-width, initial-scale=1">'
                   + '<style>body{margin:12px;overflow-wrap:anywhere}img{max-width:100%;height:auto}table{max-width:100%}</style>'
        return head + win.message.html
    }

    MailData {
        id: mail
        category: ""
        onOpened: (message) => win.message = message
        onSent: (ok, error) => {
            win.sending = false
            if (ok) {
                win.view = win.message ? "read" : "list"
                win.toast(qsTr("Sent"))
            } else {
                win.toast(error)
            }
        }
        onSaved: (path) => win.toast(qsTr("Saved to %1").arg(path.replace(/^.*\/(Downloads\/)/, "$1")))
    }
    // a message asked for (a notification tapped): at the start, or while open
    AppLink {
        name: "org.mobarchy.Mail"
        service: true
        onShowRequested: (id) => win.show(id)
    }
    function show(id) {
        if (!id)
            return
        const row = mail.messages.find(m => m.id === id)
        win.read(row ?? { id: id, subject: "", from: "", date: 0, account: id.split("|")[0] })
        win.requestActivate()
    }
    // (opened before the list had come: its row, once it has)
    Connections {
        target: mail
        function onMessagesChanged() {
            if (win.item && !win.item.subject)
                win.item = mail.messages.find(m => m.id === win.item.id) ?? win.item
        }
    }
    // mobarchy-shell --mail [message id]
    Component.onCompleted: {
        mail.sync()
        const args = Qt.application.arguments
        const i = args.indexOf("--mail")
        if (i >= 0 && i + 1 < args.length)
            Qt.callLater(() => win.show(args[i + 1]))
    }

    property string view: "list"        // "list", "read", "compose"
    property var message: null           // the one being read, in full
    property var item: null              // its row in the list
    property bool sending: false

    function when(seconds) {
        const d = new Date(seconds * 1000)
        return d.toDateString() === new Date().toDateString() ? Qt.formatTime(d, "HH:mm")
             : d.toLocaleDateString(Qt.locale(), "d MMM")
    }
    // A conversation (more than one message): one long scroll, a card for each message,
    // oldest first; a card opens (pulled out) and closes at a tap, the newest open to begin
    // with. A card shows its message's own words (its quote of the rest cut off).
    property bool threaded: false
    property var threadRow: null         // the conversation's row in the list
    property var bodies: ({})            // the opened messages, in full, by id
    property var expanded: ({})          // the open cards, by id
    function read(row) {
        item = row
        message = null
        view = "read"
        threaded = (row.count || 1) > 1
        threadRow = row
        bodies = {}
        expanded = {}
        if (threaded)
            mail.loadThread(row.id)
        else
            mail.open(row.id)
    }
    function toggle(m) {
        const open = !expanded[m.id]
        expanded = Object.assign({}, expanded, {[m.id]: open})
        if (open && !bodies[m.id])
            mail.open(m.id)
    }
    Connections {
        target: mail
        // (the newest open, once the conversation has come)
        function onConversationChanged() {
            const c = mail.conversation
            if (win.threaded && c.length && Object.keys(win.expanded).length === 0) {
                win.item = c[c.length - 1]
                win.toggle(c[c.length - 1])
            }
        }
        function onOpened(message) {
            win.bodies = Object.assign({}, win.bodies, {[message.id]: message})
        }
    }
    // what is done to a row is done to its whole conversation in the inbox
    function idsOf(row) {
        return row.thread_ids && row.thread_ids.length ? row.thread_ids : [row.id]
    }

    // --- snoozing: when ----------------------------------------------------------------------
    property string snoozing: ""     // the message's id, while choosing when
    function snoozeChoices() {
        const now = new Date()
        const at = (days, hour) => new Date(now.getFullYear(), now.getMonth(), now.getDate() + days, hour, 0)
        const out = [{ label: qsTr("Later today"), icon: "󰥔", time: new Date(now.getTime() + 3 * 3600 * 1000) }]
        if (now.getHours() < 17)
            out.push({ label: qsTr("This evening"), icon: "󰖔", time: at(0, 18) })
        out.push({ label: qsTr("Tomorrow"), icon: "󰖨", time: at(1, 8) })
        const toSaturday = (6 - now.getDay() + 7) % 7 || 7
        if (now.getDay() !== 6 && now.getDay() !== 0)
            out.push({ label: qsTr("This weekend"), icon: "󰃮", time: at(toSaturday, 9) })
        const toMonday = (8 - now.getDay()) % 7 || 7
        out.push({ label: qsTr("Next week"), icon: "󰃭", time: at(toMonday, 8) })
        return out.sort((a, b) => a.time - b.time)
    }

    // --- the list --------------------------------------------------------------------------
    Item {
        id: listPage
        anchors.fill: parent
        // (under the message as it slides in and out)
        visible: win.view === "list" || readPage.visible

        Text {
            id: heading
            anchors { left: parent.left; leftMargin: 20; top: parent.top; topMargin: 22 }
            text: mail.snoozed ? qsTr("Snoozed") : qsTr("Inbox")
            color: Theme.fg
            font { family: Theme.font; pixelSize: 26; bold: true }
        }
        Rectangle {
            id: newButton
            anchors { right: parent.right; rightMargin: 18; verticalCenter: heading.verticalCenter }
            width: 40; height: 40; radius: 20
            color: newTap.pressed ? Theme.line : Theme.panel
            Text { anchors.centerIn: parent; text: "󰏫"; color: Theme.fg; font { family: Theme.font; pixelSize: 19 } }
            MouseArea { id: newTap; anchors.fill: parent; onClicked: compose.write(null, "") }
        }

        // the snoozed: a toggle beside "new" (the inbox is one list, everything in it)
        Rectangle {
            id: snoozedToggle
            anchors { right: newButton.left; rightMargin: 10; verticalCenter: heading.verticalCenter }
            width: 40; height: 40; radius: 20
            color: mail.snoozed ? Theme.accent : (snoozedTap.pressed ? Theme.line : Theme.panel)
            Text { anchors.centerIn: parent; text: "󰒲"; color: mail.snoozed ? Theme.bg : Theme.fg
                   font { family: Theme.font; pixelSize: 19 } }
            MouseArea { id: snoozedTap; anchors.fill: parent; onClicked: mail.snoozed = !mail.snoozed }
        }
        Item {
            id: chips           // (where the list starts)
            anchors { left: parent.left; right: parent.right; top: heading.bottom; topMargin: 8 }
            height: 0
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 60
            visible: mail.messages.length === 0
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: mail.accounts.length === 0
                  ? qsTr("No mail accounts yet. Sign in to Google (with mail) or iCloud in Accounts.")
                  : mail.syncing ? qsTr("Fetching …") : mail.snoozed ? qsTr("Nothing snoozed.") : qsTr("All done 🎉")
            color: Theme.muted
            font { family: Theme.font; pixelSize: 15 }
        }
        Text {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 16 }
            visible: mail.error !== ""
            wrapMode: Text.Wrap
            text: "󰀦  " + mail.error
            color: Theme.red
            font { family: Theme.font; pixelSize: 12 }
            z: 2
        }

        // Pulled down: Frigg's orb comes out of the top, growing and turning with the pull;
        // let go past the mark, it swirls while the mail syncs (the list makes room for it).
        readonly property real pull: Math.max(0, list.originY - list.contentY)
        readonly property bool armed: pull > 90
        Item {
            id: pullZone
            anchors { left: parent.left; right: parent.right; top: chips.bottom; topMargin: 6 }
            height: 64
            visible: mail.syncing || parent.pull > 4
            FriggOrb {
                id: pullOrb
                anchors.centerIn: parent
                width: 48; height: 48
                points: 140
                color: listPage.armed || mail.syncing ? Theme.accent : Theme.muted
                state: mail.syncing ? "thinking" : listPage.armed ? "listening" : "idle"
                running: pullZone.visible
                scale: mail.syncing ? 1 : Math.min(1, 0.3 + listPage.pull / 130)
                opacity: mail.syncing ? 1 : Math.min(1, listPage.pull / 60)
                Behavior on scale { NumberAnimation { duration: 120 } }
                Behavior on color { ColorAnimation { duration: 200 } }
                // (turned as it is pulled)
                property real last: 0
                Connections {
                    target: list
                    function onContentYChanged() {
                        const p = Math.max(0, list.originY - list.contentY)
                        pullOrb.turnBy((p - pullOrb.last) * 1.5, 0)
                        pullOrb.last = p
                    }
                }
            }
        }

        ListView {
            id: list
            anchors { left: parent.left; right: parent.right; top: chips.bottom; bottom: parent.bottom
                      topMargin: mail.syncing ? 70 : 10 }
            Behavior on anchors.topMargin { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
            clip: true
            boundsBehavior: Flickable.DragAndOvershootBounds
            model: mail.messages
            // pulled down past the mark: sync
            onDragEnded: if (contentY < originY - 90) mail.sync()
            delegate: Item {
                id: row
                required property var modelData
                width: ListView.view.width
                height: leaving ? 0 : 82
                clip: true
                // Done or snoozed: out of the list the way it was swiped, then the gap closes,
                // and then it is done (snoozed: once when is chosen; back if not)
                property bool leaving: false
                function leave(toRight, then) {
                    outAnim.to = toRight ? width : -width
                    outAnim.then = then
                    leaveAnim.restart()
                }
                SequentialAnimation {
                    id: leaveAnim
                    NumberAnimation { id: outAnim; property var then; target: card; property: "x"; duration: 160; easing.type: Easing.InCubic }
                    ScriptAction { script: row.leaving = true }
                    PauseAnimation { duration: 210 }
                    ScriptAction { script: outAnim.then() }
                }
                Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }

                // What a swipe does, by how far it has come (a fifth of the row before
                // anything): to the right, read or unread (as it is not), and from half way,
                // done (archived); back to the inbox, snoozed ones. To the left, snooze. A tick
                // as each mark is passed, either way.
                readonly property real part: card.x / width
                readonly property string action: part >= 0.5 ? (mail.snoozed ? "inbox" : "done")
                                               : part >= 0.2 ? (mail.snoozed ? "inbox" : (modelData.seen ? "unread" : "read"))
                                               : part <= -0.2 && !mail.snoozed ? "snooze" : ""
                onActionChanged: if (swipe.active) haptics.tick()
                Rectangle {
                    anchors.fill: parent
                    visible: card.x !== 0
                    color: row.action === "done" || row.action === "inbox" ? Theme.green
                         : row.action === "read" || row.action === "unread" ? Theme.accent
                         : row.action === "snooze" ? Theme.magenta : Theme.panel
                    Behavior on color { ColorAnimation { duration: 120 } }
                    Text {
                        anchors { left: parent.left; leftMargin: 26; verticalCenter: parent.verticalCenter }
                        visible: card.x > 0 && row.action !== ""
                        text: ({ done: "󰄬  " + qsTr("Done"), inbox: "󰑐  " + qsTr("Inbox"),
                                 read: "󰇮  " + qsTr("Read"), unread: "󰻨  " + qsTr("Unread") })[row.action] || ""
                        color: Theme.bg
                        font { family: Theme.font; pixelSize: 16; bold: true }
                    }
                    Text {
                        anchors { right: parent.right; rightMargin: 26; verticalCenter: parent.verticalCenter }
                        visible: row.action === "snooze"
                        text: qsTr("Snooze") + "  󰒲"
                        color: Theme.bg
                        font { family: Theme.font; pixelSize: 16; bold: true }
                    }
                }
                Rectangle {
                    id: card
                    width: row.width; height: row.height
                    color: tap.pressed ? Theme.panel : Theme.bg
                    Behavior on x { enabled: !swipe.active; NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

                    Rectangle {     // unread
                        visible: !row.modelData.seen
                        anchors { left: parent.left; leftMargin: 8; top: parent.top; topMargin: 22 }
                        width: 8; height: 8; radius: 4
                        color: Theme.accent
                    }
                    Column {
                        anchors { left: parent.left; leftMargin: 24; right: parent.right; rightMargin: 20
                                  verticalCenter: parent.verticalCenter }
                        spacing: 3
                        Item {
                            width: parent.width; height: fromText.implicitHeight
                            Text {
                                id: fromText
                                anchors { left: parent.left; right: timeText.left; rightMargin: 10 }
                                elide: Text.ElideRight
                                // (a conversation: how many messages, after its newest's sender)
                                textFormat: Text.StyledText
                                text: row.modelData.from.replace(/&/g, "&amp;").replace(/</g, "&lt;")
                                      + ((row.modelData.count || 1) > 1
                                         ? "  <font color='" + Theme.muted + "'>" + row.modelData.count + "</font>" : "")
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 15; bold: !row.modelData.seen }
                            }
                            Text {
                                id: timeText
                                anchors.right: parent.right
                                text: (row.modelData.attachments ? "󰏢 " : "")
                                      + (mail.snoozed && row.modelData.snoozed_until
                                         ? "󰒲 " + win.when(row.modelData.snoozed_until) : win.when(row.modelData.date))
                                color: row.modelData.seen ? Theme.muted : Theme.accent
                                font { family: Theme.font; pixelSize: 12 }
                            }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.subject
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 14; bold: !row.modelData.seen }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideRight
                            text: row.modelData.snippet
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 12 }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; x: 24; width: parent.width - 24; height: 1; color: Theme.line; opacity: 0.4 }

                    TapHandler { id: tap; onTapped: win.read(row.modelData) }
                    DragHandler {
                        id: swipe
                        target: card
                        xAxis.enabled: true
                        yAxis.enabled: false
                        onActiveChanged: {
                            if (active)
                                return
                            const id = row.modelData.id, ids = win.idsOf(row.modelData)
                            switch (row.action) {
                            case "done": row.leave(true, () => ids.forEach(i => mail.archive(i))); return
                            case "inbox": row.leave(true, () => mail.unsnooze(id)); return
                            case "snooze": row.leave(false, () => { win.snoozing = id }); return
                            case "read": ids.forEach(i => mail.setSeen(i, true)); break
                            case "unread": ids.forEach(i => mail.setSeen(i, false)); break
                            }
                            card.x = 0
                        }
                    }
                }
            }
        }
    }

    // --- reading ---------------------------------------------------------------------------
    // Over the list, in from the right as a message opens, out to the right again as it closes.
    Item {
        id: readPage
        width: parent.width; height: parent.height
        x: win.view === "read" ? 0 : width
        Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
        visible: x < width
        Rectangle {
            anchors.fill: parent
            color: Theme.bg
            MouseArea { anchors.fill: parent }    // (the list under it takes nothing)
        }

        Item {
            id: readHead
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: "‹"; color: Theme.accent; font { family: Theme.font; pixelSize: 34 }
                MouseArea { anchors { fill: parent; margins: -12 } onClicked: win.view = "list" }
            }
        }
        // a conversation: its cards
        Flickable {
            id: threadView
            visible: win.threaded
            anchors { left: parent.left; right: parent.right; top: readHead.bottom; bottom: actions.top; bottomMargin: 8 }
            contentHeight: threadColumn.height + 24
            clip: true
            boundsBehavior: Theme.boundsBehavior
            flickDeceleration: Theme.flickDeceleration
            maximumFlickVelocity: Theme.flickVelocity
            Column {
                id: threadColumn
                x: 14
                width: threadView.width - 28
                spacing: 10
                Text {
                    x: 6
                    width: parent.width - 12
                    wrapMode: Text.Wrap
                    text: win.threadRow ? win.threadRow.subject : ""
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 21; bold: true }
                }
                Repeater {
                    model: win.threaded ? mail.conversation : []
                    delegate: Rectangle {
                        id: mcard
                        required property var modelData
                        readonly property bool open: !!win.expanded[modelData.id]
                        readonly property var body: win.bodies[modelData.id] || null
                        property bool whole: false      // its quote shown too
                        width: threadColumn.width
                        // (pulled out to its content, and back)
                        height: (open ? openPart.y + openPart.height : closedSnippet.y + closedSnippet.height) + 14
                        Behavior on height { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }
                        clip: true
                        radius: 12
                        color: cardTap.pressed ? Theme.line : Theme.panel
                        MouseArea { id: cardTap; anchors.fill: parent; onClicked: win.toggle(mcard.modelData) }
                        Item {
                            id: cardHead
                            x: 14; y: 12
                            width: parent.width - 28; height: cardFrom.implicitHeight
                            Text {
                                id: cardFrom
                                anchors { left: parent.left; right: cardWhen.left; rightMargin: 8 }
                                elide: Text.ElideRight
                                text: mcard.modelData.from
                                color: mcard.open ? Theme.accent : Theme.fg
                                font { family: Theme.font; pixelSize: 14; bold: !mcard.modelData.seen }
                            }
                            Text {
                                id: cardWhen
                                anchors.right: parent.right
                                text: win.when(mcard.modelData.date)
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 12 }
                            }
                        }
                        Text {
                            id: closedSnippet
                            x: 14; y: cardHead.y + cardHead.height + 4
                            width: parent.width - 28
                            visible: !mcard.open
                            elide: Text.ElideRight
                            text: mcard.modelData.snippet
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        Column {
                            id: openPart
                            x: 14; y: cardHead.y + cardHead.height + 6
                            width: parent.width - 28
                            spacing: 10
                            opacity: mcard.open ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: 200 } }
                            Text {
                                width: parent.width
                                wrapMode: Text.Wrap
                                text: qsTr("To %1").arg(mcard.modelData.to)
                                color: Theme.muted
                                font { family: Theme.font; pixelSize: 11 }
                            }
                            Flow {
                                width: parent.width
                                spacing: 8
                                visible: !!mcard.body && mcard.body.attachments.length > 0
                                Repeater {
                                    model: mcard.body ? mcard.body.attachments : []
                                    delegate: Rectangle {
                                        id: cfile
                                        required property var modelData
                                        width: cfileText.implicitWidth + 24; height: 34; radius: 17
                                        color: cfileTap.pressed ? Theme.line : Theme.bg
                                        Text {
                                            id: cfileText
                                            anchors.centerIn: parent
                                            text: "󰏢 " + cfile.modelData.name
                                            color: Theme.fg
                                            font { family: Theme.font; pixelSize: 13 }
                                        }
                                        MouseArea { id: cfileTap; anchors.fill: parent
                                                    onClicked: mail.saveAttachment(mcard.modelData.id, cfile.modelData.index) }
                                    }
                                }
                            }
                            TextEdit {
                                width: parent.width
                                readOnly: true
                                activeFocusOnPress: false  // (no keyboard for reading)
                                selectByMouse: false
                                wrapMode: TextEdit.Wrap
                                textFormat: TextEdit.PlainText
                                text: !mcard.body ? qsTr("Opening …") : mcard.whole ? mcard.body.text : (mcard.body.fresh || mcard.body.text)
                                color: mcard.body ? Theme.fg : Theme.muted
                                font { family: Theme.font; pixelSize: 15 }
                            }
                            // the quote cut off: shown at a tap
                            Rectangle {
                                visible: !!mcard.body && !!mcard.body.fresh && mcard.body.fresh !== mcard.body.text
                                width: 44; height: 22; radius: 11
                                color: wholeTap.pressed ? Theme.line : Theme.bg
                                Text { anchors.centerIn: parent; text: "···"; color: Theme.muted
                                       font { family: Theme.font; pixelSize: 14; bold: true } }
                                MouseArea { id: wholeTap; anchors { fill: parent; margins: -8 }
                                            onClicked: mcard.whole = !mcard.whole }
                            }
                        }
                    }
                }
            }
        }
        Flickable {
            id: reader
            visible: !win.threaded
            anchors { left: parent.left; right: parent.right; top: readHead.bottom }
            // (an HTML mail: the headers above, its own view below them)
            height: win.html ? Math.min(contentHeight, parent.height * 0.4)
                             : parent.height - readHead.height - actions.height - 14
            contentHeight: readColumn.height + (win.html ? 12 : 40)
            clip: true
            boundsBehavior: Theme.boundsBehavior
            Column {
                id: readColumn
                x: 20
                width: reader.width - 40
                spacing: 12
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: win.message ? win.message.subject : win.item ? win.item.subject : ""
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 21; bold: true }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: win.message ? win.message.from : (win.item ? win.item.from : "")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 14 }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.Wrap
                    visible: !!win.message
                    text: win.message ? qsTr("To %1").arg(win.message.to) + (win.message.cc ? "\n" + qsTr("Cc %1").arg(win.message.cc) : "")
                                        + "\n" + (win.item && win.item.date ? new Date(win.item.date * 1000).toLocaleString(Qt.locale(), "d MMMM yyyy HH:mm")
                                                   : win.message.date)
                                      : ""
                    color: Theme.muted
                    font { family: Theme.font; pixelSize: 12 }
                }
                // attachments: a tap saves one to Downloads
                Flow {
                    width: parent.width
                    spacing: 8
                    visible: !!win.message && win.message.attachments.length > 0
                    Repeater {
                        model: win.message ? win.message.attachments : []
                        delegate: Rectangle {
                            id: file
                            required property var modelData
                            width: fileText.implicitWidth + 24; height: 34; radius: 17
                            color: fileTap.pressed ? Theme.line : Theme.panel
                            Text {
                                id: fileText
                                anchors.centerIn: parent
                                text: "󰏢 " + file.modelData.name
                                color: Theme.fg
                                font { family: Theme.font; pixelSize: 13 }
                            }
                            MouseArea {
                                id: fileTap
                                anchors.fill: parent
                                onClicked: mail.saveAttachment(win.item.id, file.modelData.index)
                            }
                        }
                    }
                }
                // remote images (and with them, trackers) only when asked for
                Rectangle {
                    visible: win.html && win.remoteImages && !win.showImages
                    width: imagesText.implicitWidth + 28; height: 34; radius: 17
                    color: imagesTap.pressed ? Theme.line : Theme.panel
                    Text {
                        id: imagesText
                        anchors.centerIn: parent
                        text: "󰋩  " + qsTr("Show images")
                        color: Theme.fg
                        font { family: Theme.font; pixelSize: 13 }
                    }
                    MouseArea { id: imagesTap; anchors.fill: parent; onClicked: win.showImages = true }
                }
                Rectangle { width: parent.width; height: 1; color: Theme.line }
                TextEdit {
                    visible: !win.html
                    width: parent.width
                    readOnly: true
                    activeFocusOnPress: false  // (no keyboard for reading)
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: TextEdit.PlainText
                    text: win.message ? win.message.text : qsTr("Opening …")
                    color: win.message ? Theme.fg : Theme.muted
                    selectionColor: Theme.accent
                    font { family: Theme.font; pixelSize: 15 }
                }
            }
        }
        // an HTML mail, as it was made (Chromium: no scripts; remote content only with
        // "show images"; links open in the browser)
        Loader {
            anchors { left: parent.left; right: parent.right; top: reader.bottom; bottom: actions.top; bottomMargin: 8 }
            active: win.html && win.view === "read" && !win.threaded
            sourceComponent: WebEngineView {
                id: htmlView
                backgroundColor: "white"
                settings.javascriptEnabled: false
                settings.localContentCanAccessRemoteUrls: false
                settings.autoLoadImages: true
                Component.onCompleted: htmlView.loadHtml(win.htmlPage(), "about:blank")
                Connections {
                    target: win
                    function onShowImagesChanged() { htmlView.loadHtml(win.htmlPage(), "about:blank") }
                }
                onNavigationRequested: (request) => {
                    if (request.navigationType === WebEngineNavigationRequest.LinkClickedNavigation) {
                        Qt.openUrlExternally(request.url)
                        request.reject()
                    }
                }
                // (links for a new window, target="_blank", as most mail has them)
                onNewWindowRequested: (request) => Qt.openUrlExternally(request.requestedUrl)
            }
        }
        Row {
            id: actions
            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 14 }
            spacing: 26
            Repeater {
                model: [{ id: "reply", icon: "󰑚", label: qsTr("Reply") }, { id: "forward", icon: "󰒊", label: qsTr("Forward") },
                        { id: "archive", icon: "󰀼", label: qsTr("Archive") }, { id: "snooze", icon: "󰒲", label: qsTr("Snooze") },
                        { id: "trash", icon: "󰆴", label: qsTr("Delete") }]
                delegate: Item {
                    id: act
                    required property var modelData
                    // (the tap area over the icon and its label: a MouseArea in a Column would be
                    // placed under them)
                    width: Math.max(actColumn.width, 56); height: actColumn.height
                    Column {
                        id: actColumn
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: 4
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: act.modelData.icon
                            color: act.modelData.id === "trash" ? Theme.red : Theme.fg
                            font { family: Theme.font; pixelSize: 24 }
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: act.modelData.label
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 11 }
                        }
                    }
                    MouseArea {
                        anchors { fill: parent; margins: -10 }
                        onClicked: {
                            const id = win.item.id
                            switch (act.modelData.id) {
                            case "reply": compose.write(win.message, "reply"); break
                            case "forward": compose.write(win.message, "forward"); break
                            case "archive": win.idsOf(win.threadRow || win.item).forEach(i => mail.archive(i)); win.view = "list"; break
                            case "snooze": win.snoozing = id; break
                            case "trash": mail.trash(id); win.view = "list"; break
                            }
                        }
                    }
                }
            }
        }
    }

    // --- writing ---------------------------------------------------------------------------
    Item {
        id: compose
        anchors.fill: parent
        visible: win.view === "compose"
        property string account: ""
        property string replyTo: ""

        // a new one, a reply or a forward of `original`
        function write(original, how) {
            replyTo = how === "reply" && win.item ? win.item.id : ""
            account = win.item && original ? win.item.account : (mail.accounts.length ? mail.accounts[0].id : "")
            const subject = original ? original.subject : ""
            const quoted = original ? "\n\n" + qsTr("On %1, %2 wrote:").arg(original.date).arg(original.from) + "\n"
                                      + original.text.split("\n").map(l => "> " + l).join("\n") : ""
            compose.toField.text = how === "reply" ? (original.reply_to || original.from) : ""
            compose.ccField.text = ""
            compose.subjectField.text = how === "reply" ? (/^re:/i.test(subject) ? subject : "Re: " + subject)
                              : how === "forward" ? "Fwd: " + subject : ""
            bodyField.text = how === "forward" ? "\n\n" + qsTr("Forwarded:") + "\n" + qsTr("From: %1").arg(original.from)
                                                  + "\n" + qsTr("Subject: %1").arg(subject) + "\n\n" + original.text
                           : quoted
            bodyField.cursorPosition = 0
            win.view = "compose"
            if (how === "reply")
                bodyField.forceActiveFocus()
            else
                compose.toField.forceActiveFocus()
            // (focus given in code doesn't ask for the keyboard; a tap does)
            Qt.inputMethod.show()
        }

        Item {
            id: composeHead
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            Text {
                anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                text: "‹"; color: Theme.accent; font { family: Theme.font; pixelSize: 34 }
                MouseArea { anchors { fill: parent; margins: -12 } onClicked: win.view = win.message ? "read" : "list" }
            }
            Rectangle {
                anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
                width: 76; height: 38; radius: 19
                readonly property bool ready: compose.toField.text.trim() !== "" && compose.account !== "" && !win.sending
                color: ready ? Theme.accent : Theme.line
                Text {
                    anchors.centerIn: parent
                    text: win.sending ? "…" : qsTr("Send")
                    color: Theme.bg
                    font { family: Theme.font; pixelSize: 14; bold: true }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        if (!parent.ready)
                            return
                        win.sending = true
                        mail.send(compose.account, compose.toField.text.trim(), compose.ccField.text.trim(), compose.subjectField.text,
                                  bodyField.text, compose.replyTo)
                    }
                }
            }
        }
        component Line: Item {
            id: line
            property string label
            property alias input: field
            width: parent ? parent.width : 0
            height: 46
            Text {
                id: lineLabel
                anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                width: 70
                text: line.label
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
            TextInput {
                id: field
                anchors { left: lineLabel.right; right: parent.right; verticalCenter: parent.verticalCenter }
                clip: true
                color: Theme.fg
                font { family: Theme.font; pixelSize: 15 }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line; opacity: 0.5 }
        }
        Column {
            id: fields
            anchors { left: parent.left; right: parent.right; top: composeHead.bottom; leftMargin: 20; rightMargin: 20 }
            // from which account (when there are several)
            Item {
                width: parent.width
                height: mail.accounts.length > 1 ? 46 : 0
                visible: mail.accounts.length > 1
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("From") + "      " + (mail.accounts.find(a => a.id === compose.account)?.user ?? "") + "  ⌄"
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 14 }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        const ids = mail.accounts.map(a => a.id)
                        compose.account = ids[(ids.indexOf(compose.account) + 1) % ids.length]
                    }
                }
            }
            Line { id: toLine; label: qsTr("To") }
            Line { id: ccLine; label: qsTr("Cc") }
            Line { id: subjectLine; label: qsTr("Subject") }
        }
        readonly property alias toField: toLine.input
        readonly property alias ccField: ccLine.input
        readonly property alias subjectField: subjectLine.input
        Flickable {
            id: bodyFlick
            anchors { left: parent.left; right: parent.right; top: fields.bottom; bottom: parent.bottom; margins: 20 }
            contentHeight: bodyField.implicitHeight + 40
            clip: true
            boundsBehavior: Theme.boundsBehavior
            TextEdit {
                id: bodyField
                width: bodyFlick.width
                wrapMode: TextEdit.Wrap
                color: Theme.fg
                selectionColor: Theme.accent
                font { family: Theme.font; pixelSize: 15 }
                Text {
                    visible: bodyField.text === "" && !bodyField.activeFocus
                    text: qsTr("Write …")
                    color: Theme.muted
                    font: bodyField.font
                }
            }
        }
    }

    // --- snooze: when ------------------------------------------------------------------------
    Item {
        anchors.fill: parent
        visible: win.snoozing !== ""
        z: 5
        Rectangle {
            anchors.fill: parent
            color: Qt.alpha(Theme.bgDark, 0.7)
            // (not snoozed after all: the list as it is, the message back in it)
            MouseArea { anchors.fill: parent; onClicked: { win.snoozing = ""; mail.refresh() } }
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: snoozeColumn.height + 40
            radius: 24
            color: Theme.bg
            border.color: Theme.line
            Column {
                id: snoozeColumn
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
                spacing: 6
                Text {
                    text: qsTr("Snooze until …")
                    color: Theme.fg
                    font { family: Theme.font; pixelSize: 18; bold: true }
                    bottomPadding: 8
                }
                Repeater {
                    model: win.snoozing !== "" ? win.snoozeChoices() : []
                    delegate: Rectangle {
                        id: choice
                        required property var modelData
                        width: snoozeColumn.width; height: 56; radius: 14
                        color: choiceTap.pressed ? Theme.line : Theme.panel
                        Text {
                            anchors { left: parent.left; leftMargin: 18; verticalCenter: parent.verticalCenter }
                            text: choice.modelData.icon + "   " + choice.modelData.label
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 15 }
                        }
                        Text {
                            anchors { right: parent.right; rightMargin: 18; verticalCenter: parent.verticalCenter }
                            text: choice.modelData.time.toLocaleString(Qt.locale(), "ddd HH:mm")
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        MouseArea {
                            id: choiceTap
                            anchors.fill: parent
                            onClicked: {
                                mail.snooze(win.snoozing, Math.floor(choice.modelData.time.getTime() / 1000))
                                win.snoozing = ""
                                win.view = "list"
                            }
                        }
                    }
                }
            }
        }
    }

    // --- a word, for a moment ----------------------------------------------------------------
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
            width: Math.min(implicitWidth, win.width - 72)
            wrapMode: Text.Wrap
            color: Theme.bg
            font { family: Theme.font; pixelSize: 14 }
        }
        SequentialAnimation {
            id: toastFade
            PauseAnimation { duration: 1800 }
            NumberAnimation { target: toastBox; property: "opacity"; to: 0; duration: 300 }
        }
    }
}
