pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// Accounts (ADR 0032): the owner's internet accounts, whose contacts the phone keeps: Google and
// Nextcloud (signing in in the browser), iCloud (an app-specific password) and other CardDAV
// servers.
// Read-only: nothing is written to the accounts. Each address book can be turned off. Its own
// window (mobarchy-shell --accounts); the work is mobarchy-accounts' (Accounts).
Window {
    id: win

    visible: true
    title: qsTr("Accounts")
    color: Theme.bg

    Accounts {
        id: accounts
        onAdded: win.page = "list"
    }

    // "list", "icloud", "google", "nextcloud", "carddav"
    property string page: "list"

    function ago(epoch) {
        if (!epoch)
            return qsTr("not synced yet")
        const m = Math.round((Date.now() / 1000 - epoch) / 60)
        return m < 1 ? qsTr("synced just now") : m < 60 ? qsTr("synced %1 min ago").arg(m)
             : qsTr("synced %1").arg(Qt.formatDateTime(new Date(epoch * 1000), "d. MMM HH:mm"))
    }

    component Field: Rectangle {
        id: field
        property alias text: input.text
        property string hint
        property bool secret: false
        width: parent.width
        height: 48
        radius: 12
        color: Theme.panel
        TextInput {
            id: input
            anchors { fill: parent; leftMargin: 16; rightMargin: 16 }
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.fg
            echoMode: field.secret ? TextInput.Password : TextInput.Normal
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            font { family: Theme.font; pixelSize: 16 }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: parent.text === ""
                text: field.hint
                color: Theme.muted
                font: parent.font
            }
        }
    }

    component Button: Rectangle {
        id: button
        property string label
        property bool primary: true
        signal clicked()
        width: Math.max(140, buttonText.implicitWidth + 40)
        height: 46
        radius: 23
        opacity: enabled ? 1 : 0.4
        color: buttonTap.pressed ? Theme.line : primary ? Theme.accent : Theme.panel
        Text {
            id: buttonText
            anchors.centerIn: parent
            text: button.label
            color: button.primary ? Theme.bg : Theme.fg
            font { family: Theme.font; pixelSize: 15; bold: button.primary }
        }
        TapHandler { id: buttonTap; onTapped: button.clicked() }
    }

    component Help: Text {
        width: parent.width
        wrapMode: Text.Wrap
        color: Theme.muted
        textFormat: Text.StyledText
        font { family: Theme.font; pixelSize: 13 }
    }

    // --- the header ---
    Item {
        id: head
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 70
        Text {
            visible: win.page !== "list"
            anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
            text: "‹"
            color: Theme.accent
            font { family: Theme.font; pixelSize: 36 }
            TapHandler { onTapped: { accounts.cancel(); win.page = "list" } }
        }
        Text {
            anchors { left: parent.left; leftMargin: win.page === "list" ? 20 : 52; verticalCenter: parent.verticalCenter }
            text: ({ list: qsTr("Accounts"), icloud: "iCloud", google: "Google", nextcloud: "Nextcloud",
                    carddav: qsTr("Other (CardDAV)") })[win.page]
            color: Theme.fg
            font { family: Theme.font; pixelSize: 24; bold: true }
        }
    }

    // what is going on, or what went wrong
    Rectangle {
        id: status
        anchors { left: parent.left; right: parent.right; top: head.bottom; margins: 16 }
        visible: accounts.busy !== "" || accounts.error !== ""
        height: visible ? statusText.implicitHeight + 24 : 0
        radius: 12
        color: accounts.error !== "" ? Qt.alpha(Theme.red, 0.18) : Theme.panel
        Text {
            id: statusText
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: 14 }
            wrapMode: Text.Wrap
            text: accounts.busy !== "" ? accounts.busy : accounts.error
            color: accounts.error !== "" ? Theme.red : Theme.fg
            font { family: Theme.font; pixelSize: 14 }
        }
    }

    // --- the accounts ---
    Flickable {
        anchors { left: parent.left; right: parent.right; top: status.bottom; topMargin: 8; bottom: parent.bottom }
        visible: win.page === "list"
        contentHeight: list.height + 40
        clip: true
        boundsBehavior: Theme.boundsBehavior

        Column {
            id: list
            x: 16
            width: parent.width - 32
            spacing: 14

            Help {
                visible: accounts.accounts.length === 0
                text: qsTr("Add your Google, iCloud and Nextcloud accounts to have their contacts on the phone: in Phone, in Messages and for Frigg. Nothing on the accounts is changed.")
            }

            Repeater {
                model: accounts.accounts
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    width: list.width
                    height: cardColumn.implicitHeight + 28
                    radius: 16
                    color: Theme.panel
                    Column {
                        id: cardColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 14 }
                        spacing: 6
                        Text {
                            text: ({ icloud: "󰀸  iCloud", google: "󰊭  Google", nextcloud: "󰅟  Nextcloud",
                                    carddav: "󰃭  CardDAV" })[card.modelData.type] ?? card.modelData.type
                            color: Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        Text {
                            width: parent.width
                            elide: Text.ElideMiddle
                            text: card.modelData.user
                            color: Theme.fg
                            font { family: Theme.font; pixelSize: 17 }
                        }
                        Text {
                            width: parent.width
                            wrapMode: Text.Wrap
                            text: card.modelData.error !== "" ? card.modelData.error
                                  : qsTr("%n contact(s)", "", card.modelData.contacts) + " · " + win.ago(card.modelData.last_sync)
                            color: card.modelData.error !== "" ? Theme.red : Theme.muted
                            font { family: Theme.font; pixelSize: 13 }
                        }
                        Repeater {
                            model: card.modelData.books
                            delegate: Item {
                                id: book
                                required property var modelData
                                width: cardColumn.width
                                height: 40
                                Text {
                                    anchors { left: parent.left; right: bookSwitch.left; rightMargin: 10; verticalCenter: parent.verticalCenter }
                                    elide: Text.ElideRight
                                    text: book.modelData.name
                                    color: Theme.fg
                                    font { family: Theme.font; pixelSize: 15 }
                                }
                                Switch {
                                    id: bookSwitch
                                    anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                                    checked: book.modelData.enabled
                                    onToggled: accounts.setBook(card.modelData.id, book.modelData.url, !book.modelData.enabled)
                                }
                            }
                        }
                        Text {
                            text: qsTr("Remove")
                            color: Theme.red
                            font { family: Theme.font; pixelSize: 14 }
                            topPadding: 6
                            TapHandler { onTapped: accounts.remove(card.modelData.id) }
                        }
                    }
                }
            }

            Item { width: 1; height: 6 }
            Text {
                text: qsTr("Add an account")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 13 }
            }
            Row {
                spacing: 10
                Button { label: "Google"; primary: false; onClicked: win.page = "google" }
                Button { label: "iCloud"; primary: false; onClicked: win.page = "icloud" }
            }
            Row {
                spacing: 10
                Button { label: "Nextcloud"; primary: false; onClicked: win.page = "nextcloud" }
                Button { label: qsTr("Other (CardDAV)"); primary: false; onClicked: win.page = "carddav" }
            }
            Button {
                visible: accounts.accounts.length > 0
                label: qsTr("Sync now")
                enabled: accounts.busy === ""
                onClicked: accounts.sync()
            }
        }
    }

    // --- iCloud ---
    Column {
        anchors { left: parent.left; right: parent.right; top: status.bottom; margins: 16 }
        visible: win.page === "icloud"
        spacing: 12
        Help {
            text: qsTr("Apple lets other apps in with an <b>app-specific password</b> only (not your Apple ID password). Three steps:")
        }
        Help { text: qsTr("<b>1.</b> Open Apple's page and sign in: Sign-In and Security → App-Specific Passwords → +, name it \"Mobarchy\".") }
        Button {
            label: qsTr("Open Apple's page")
            primary: false
            onClicked: accounts.openUrl("https://account.apple.com/account/manage")
        }
        Help { text: qsTr("<b>2.</b> Copy the password Apple shows (xxxx-xxxx-xxxx-xxxx). <b>3.</b> Come back here and paste it.") }
        Field { id: appleId; hint: qsTr("Apple ID (e-mail)") }
        Field { id: applePw; hint: qsTr("App-specific password"); secret: true }
        Button {
            label: qsTr("Add")
            enabled: accounts.busy === "" && appleId.text !== "" && applePw.text !== ""
            onClicked: { accounts.addIcloud(appleId.text, applePw.text); applePw.text = "" }
        }
    }

    // --- Google ---
    Column {
        anchors { left: parent.left; right: parent.right; top: status.bottom; margins: 16 }
        visible: win.page === "google"
        spacing: 12
        // a client first: once, for every Google account
        Help {
            visible: !accounts.googleClient
            text: qsTr("This build of Mobarchy has no Google sign-in client of its own, so Google wants yours, made once for all your Google accounts: in <b>console.cloud.google.com</b>, a project with the <b>CardDAV API</b> on, an OAuth consent screen (External, published), and an OAuth client of type <b>Desktop app</b>. Give its id and secret here.")
        }
        Field { id: clientId; visible: !accounts.googleClient; hint: qsTr("Client ID (….apps.googleusercontent.com)") }
        Field { id: clientSecret; visible: !accounts.googleClient; hint: qsTr("Client secret"); secret: true }
        Button {
            visible: !accounts.googleClient
            label: qsTr("Save")
            enabled: accounts.busy === "" && clientId.text !== "" && clientSecret.text !== ""
            onClicked: { accounts.setGoogleClient(clientId.text, clientSecret.text); clientSecret.text = "" }
        }
        Help {
            visible: accounts.googleClient
            text: qsTr("Sign in to Google in the browser, and allow Mobarchy to read your contacts. Nothing on the account is changed. Add as many Google accounts as you like, one at a time.")
        }
        Button {
            visible: accounts.googleClient
            label: qsTr("Sign in with Google")
            enabled: accounts.busy === ""
            onClicked: accounts.addGoogle()
        }
    }

    // --- Nextcloud: signing in in the browser ---
    Column {
        anchors { left: parent.left; right: parent.right; top: status.bottom; margins: 16 }
        visible: win.page === "nextcloud"
        spacing: 12
        Help { text: qsTr("Your Nextcloud's address. You sign in in the browser, and Nextcloud gives the phone a password of its own (you can revoke it in Nextcloud's security settings).") }
        Field { id: ncUrl; hint: "cloud.example.org" }
        Button {
            label: qsTr("Sign in")
            enabled: accounts.busy === "" && ncUrl.text !== ""
            onClicked: accounts.addNextcloud(ncUrl.text)
        }
    }

    // --- another CardDAV server ---
    Column {
        anchors { left: parent.left; right: parent.right; top: status.bottom; margins: 16 }
        visible: win.page === "carddav"
        spacing: 12
        Help { text: qsTr("A server with CardDAV (Fastmail, Radicale, …): its address, your user name and a password (an app password where the server has them).") }
        Field { id: davUrl; hint: "https://…" }
        Field { id: davUser; hint: qsTr("User name") }
        Field { id: davPw; hint: qsTr("Password"); secret: true }
        Button {
            label: qsTr("Add")
            enabled: accounts.busy === "" && davUrl.text !== "" && davUser.text !== "" && davPw.text !== ""
            onClicked: { accounts.addCarddav(davUrl.text, davUser.text, davPw.text); davPw.text = "" }
        }
    }
}
