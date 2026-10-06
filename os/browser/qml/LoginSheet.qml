import QtQuick
import Mobarchy.Browser

// 1Password's logins for this site: a sheet from the bottom. A tap fills one in. If it has
// a one-time code, the sheet offers it next time on that site (the code page after it).
Item {
    id: sheet
    property string host
    property var logins: []
    property string error
    // The last login filled in on each site that has a one-time code: the code page that
    // follows gets it (fetched fresh: codes change every 30 s). host -> {id, title}
    property var codeFor: ({})
    property bool fetchingCode: false
    signal fill(string username, string password)
    signal fillCode(string code)

    visible: false
    OnePassword {
        id: op
        onListed: (logins, error) => { sheet.logins = logins; sheet.error = error }
        onCredentialsReady: (username, password, otp, error) => {
            if (error) {
                sheet.error = error
                return
            }
            if (sheet.fetchingCode) {
                sheet.fetchingCode = false
                if (otp)
                    sheet.fillCode(otp)
            } else {
                sheet.fill(username, password)
                const c = Object.assign({}, sheet.codeFor)
                if (otp)
                    c[sheet.host] = {id: sheet.picked.id, title: sheet.picked.title}
                else
                    delete c[sheet.host]
                sheet.codeFor = c
            }
            sheet.close()
        }
    }
    readonly property bool available: op.available

    property var picked: ({})
    readonly property var code: codeFor[host] || null
    function open(forHost) {
        host = forHost
        logins = []
        error = ""
        visible = true
        op.logins(forHost)
    }
    function close() { visible = false }

    Rectangle {
        anchors.fill: parent; color: "#80000000"
        MouseArea { anchors.fill: parent; onClicked: sheet.close() }
    }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Math.min(parent.height * 0.7, content.implicitHeight + 40)
        radius: 20
        color: Theme.panel
        MouseArea { anchors.fill: parent }
        Column {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20 }
            spacing: 12
            Text {
                text: "󰌋  " + qsTr("1Password · %1").arg(sheet.host)
                color: Theme.fg
                font { family: Theme.font; pixelSize: 16; bold: true }
            }
            Text {
                visible: op.busy && !sheet.logins.length
                text: qsTr("Asking 1Password …")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
            Text {
                visible: sheet.error.length > 0
                width: parent.width
                text: sheet.error
                color: Theme.red; wrapMode: Text.Wrap
                font { family: Theme.font; pixelSize: 14 }
            }
            Text {
                visible: !op.busy && !sheet.error && !sheet.logins.length
                width: parent.width
                text: qsTr("No logins for this site in 1Password.")
                color: Theme.muted; wrapMode: Text.Wrap
                font { family: Theme.font; pixelSize: 14 }
            }
            // the one-time code of the login filled in here before
            Rectangle {
                visible: sheet.code !== null
                width: parent.width; height: 60; radius: 14
                color: codeTap.pressed ? Theme.line : Theme.bg
                Text {
                    anchors { left: parent.left; leftMargin: 16; verticalCenter: parent.verticalCenter }
                    text: "󰦯  " + qsTr("One-time code · %1").arg(sheet.code ? sheet.code.title : "")
                    color: Theme.accent
                    font { family: Theme.font; pixelSize: 16; bold: true }
                }
                MouseArea {
                    id: codeTap; anchors.fill: parent
                    enabled: !op.busy
                    onClicked: { sheet.fetchingCode = true; op.credentials(sheet.code.id) }
                }
            }
            Repeater {
                model: sheet.logins
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    width: content.width; height: 64; radius: 14
                    color: rowTap.pressed ? Theme.line : Theme.bg
                    Column {
                        anchors { left: parent.left; leftMargin: 16; right: parent.right; rightMargin: 16
                                  verticalCenter: parent.verticalCenter }
                        Text {
                            width: parent.width
                            text: row.modelData.title; color: Theme.fg; elide: Text.ElideRight
                            font { family: Theme.font; pixelSize: 15 }
                        }
                        Text {
                            width: parent.width
                            text: row.modelData.username; color: Theme.muted; elide: Text.ElideRight
                            font { family: Theme.font; pixelSize: 12 }
                        }
                    }
                    MouseArea {
                        id: rowTap; anchors.fill: parent
                        enabled: !op.busy
                        onClicked: { sheet.picked = row.modelData; op.credentials(row.modelData.id) }
                    }
                }
            }
        }
    }
}
