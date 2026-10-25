import QtQuick
import QtWebEngine

// A page asking for the camera, the microphone or where the phone is: the owner says yes or
// no (ask(view, permission) shows it).
Item {
    id: permissionSheet
    anchors.fill: parent
    visible: false
    property var permission: null
    property string host
    function ask(view, p) {
        permission = p
        host = p.origin.toString() ? new URL(p.origin.toString()).hostname : ""
        visible = true
    }
    function what(p) {
        switch (p.permissionType) {
        case WebEnginePermission.PermissionType.MediaAudioCapture: return qsTr("use the microphone")
        case WebEnginePermission.PermissionType.MediaVideoCapture: return qsTr("use the camera")
        case WebEnginePermission.PermissionType.MediaAudioVideoCapture: return qsTr("use the camera and the microphone")
        case WebEnginePermission.PermissionType.Geolocation: return qsTr("know where you are")
        case WebEnginePermission.PermissionType.Notifications: return qsTr("show notifications")
        case WebEnginePermission.PermissionType.ClipboardReadWrite: return qsTr("read the clipboard")
        }
        return qsTr("use a feature of the phone")
    }
    Rectangle { anchors.fill: parent; color: "#80000000" }
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 190; radius: 20
        color: Theme.panel
        Column {
            anchors { fill: parent; margins: 20 }
            spacing: 18
            Text {
                width: parent.width
                text: permissionSheet.permission
                      ? qsTr("%1 wants to %2").arg(permissionSheet.host).arg(permissionSheet.what(permissionSheet.permission)) : ""
                color: Theme.fg; wrapMode: Text.Wrap
                font { family: Theme.font; pixelSize: 16 }
            }
            Row {
                spacing: 12
                Rectangle {
                    width: 130; height: 50; radius: 25; color: Theme.accent
                    Text { anchors.centerIn: parent; text: qsTr("Allow"); color: Theme.bg
                           font { family: Theme.font; pixelSize: 15; bold: true } }
                    MouseArea { anchors.fill: parent
                                onClicked: { permissionSheet.permission.grant(); permissionSheet.visible = false } }
                }
                Rectangle {
                    width: 130; height: 50; radius: 25; color: Theme.line
                    Text { anchors.centerIn: parent; text: qsTr("Don't allow"); color: Theme.fg
                           font { family: Theme.font; pixelSize: 15 } }
                    MouseArea { anchors.fill: parent
                                onClicked: { permissionSheet.permission.deny(); permissionSheet.visible = false } }
                }
            }
        }
    }
}
