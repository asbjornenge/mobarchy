import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// The agent asks to take a step the owner approves (ADR 0024): what it wants to do, the
// command in full, and the owner's answer. Over everything, as the lock screen is.
LayerWindow {
    id: sheet

    required property Approvals approvals
    required property Haptics haptics

    shown: approvals.pending && !approvals.locked
    onShownChanged: if (shown) haptics.click()
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-approval"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    Rectangle {
        anchors.fill: parent
        color: "#b0000000"
        MouseArea { anchors.fill: parent }     // (taps stay here)
    }

    Rectangle {
        id: card
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 12 }
        height: content.implicitHeight + 48
        radius: 24
        color: Theme.panel
        border.color: Theme.line

        Column {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 24 }
            spacing: 16

            Text {
                width: parent.width
                text: "󰚩  " + qsTr("The agent wants to")
                color: Theme.muted
                font { family: Theme.font; pixelSize: 14 }
            }
            Text {
                width: parent.width
                text: sheet.approvals.title
                color: Theme.fg
                wrapMode: Text.Wrap
                font { family: Theme.font; pixelSize: 20; bold: true }
            }
            // the command, or the call's arguments: all of it, scrolling if long
            Rectangle {
                visible: sheet.approvals.detail.length > 0
                width: parent.width
                height: Math.min(detailText.implicitHeight + 28, sheet.height * 0.4)
                radius: 14
                color: Theme.bgDark
                border.color: Theme.line
                Flickable {
                    id: detailFlick
                    anchors { fill: parent; margins: 14 }
                    contentHeight: detailText.implicitHeight
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    maximumFlickVelocity: Theme.flickVelocity
                    flickDeceleration: Theme.flickDeceleration
                    Text {
                        id: detailText
                        width: detailFlick.width
                        text: sheet.approvals.detail
                        color: Theme.green
                        wrapMode: Text.WrapAnywhere
                        textFormat: Text.PlainText
                        font { family: Theme.font; pixelSize: 14 }
                    }
                }
            }

            Item { width: 1; height: 4 }

            Repeater {
                model: [
                    {answer: "once", label: qsTr("Allow"), fill: Theme.accent, text: Theme.bg},
                    {answer: "task", label: qsTr("Allow for this task"), fill: Theme.bg, text: Theme.fg},
                    {answer: "deny", label: qsTr("Deny"), fill: Theme.bg, text: Theme.red},
                ]
                delegate: Rectangle {
                    id: button
                    required property var modelData
                    width: content.width
                    height: 56
                    radius: 28
                    color: tap.pressed ? Theme.line : button.modelData.fill
                    border.color: Theme.line
                    Text {
                        anchors.centerIn: parent
                        text: button.modelData.label
                        color: button.modelData.text
                        font { family: Theme.font; pixelSize: 16; bold: button.modelData.answer === "once" }
                    }
                    MouseArea {
                        id: tap
                        anchors.fill: parent
                        onClicked: sheet.approvals.answer(button.modelData.answer)
                    }
                }
            }
        }
    }
}
