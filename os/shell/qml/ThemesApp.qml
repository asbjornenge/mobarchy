pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import Mobarchy.Shell

// The themes (ADR 0025): Mobarchy's own and the owner's, each as a card in its own colours.
// A tap switches (the whole phone, at once); holding one of the owner's offers to delete it.
// New ones come from the agent. Its own window (mobarchy-shell --themes), like the Store.
Window {
    id: win

    visible: true
    title: qsTr("Themes")
    color: Theme.bg

    ThemeLibrary { id: library }
    property string deleting: ""    // the card that offers "Delete"

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 48
        boundsBehavior: Theme.boundsBehavior
        maximumFlickVelocity: Theme.flickVelocity
        rebound: Transition { NumberAnimation { properties: "x,y"; duration: Theme.reboundDuration; easing.type: Easing.OutCubic } }
        flickDeceleration: Theme.flickDeceleration

        Column {
            id: column
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 20; topMargin: 28 }
            spacing: 16

            Text {
                text: qsTr("Themes")
                color: Theme.fg
                font { family: Theme.font; pixelSize: 26; bold: true }
            }

            Repeater {
                model: library.themes
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    readonly property var c: modelData.colors
                    width: column.width
                    height: 150
                    radius: 22
                    color: c.bg
                    // (rounded corners for the wallpaper too: clip only cuts rectangles)
                    layer.enabled: modelData.wallpaper !== ""
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: ShaderEffectSource {
                            sourceItem: Rectangle { width: card.width; height: card.height; radius: card.radius }
                        }
                    }

                    // its wallpaper, dimmed as the lock screen dims it
                    Image {
                        anchors.fill: parent
                        visible: card.modelData.wallpaper !== ""
                        source: card.modelData.wallpaper
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize.width: 600
                    }
                    Rectangle {
                        anchors.fill: parent
                        visible: card.modelData.wallpaper !== ""
                        color: card.c.bg
                        opacity: 0.75    // (more than the lock screen: the card has text over it)
                    }
                    // the border over the wallpaper; the active theme's in its accent
                    Rectangle {
                        anchors.fill: parent
                        z: 1
                        radius: card.radius
                        color: "transparent"
                        border.width: card.modelData.active ? 3 : 1
                        border.color: card.modelData.active ? card.c.accent : card.c.line
                    }

                    Column {
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 18 }
                        spacing: 6
                        Row {
                            spacing: 8
                            Text {
                                text: card.modelData.name
                                color: card.c.fg
                                font { family: card.modelData.font; pixelSize: 19; bold: true }
                            }
                            Text {
                                visible: card.modelData.active
                                anchors.verticalCenter: parent.verticalCenter
                                text: "󰄬"
                                color: card.c.accent
                                font { family: Theme.font; pixelSize: 18 }
                            }
                        }
                        Text {
                            width: parent.width
                            text: card.modelData.description
                            color: card.c.muted
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font { family: card.modelData.font; pixelSize: 13 }
                        }
                    }
                    // the colours, as on a paint card
                    Row {
                        anchors { left: parent.left; bottom: parent.bottom; margins: 18 }
                        spacing: 8
                        Repeater {
                            model: ["panel", "accent", "fg", "green", "red", "magenta"]
                            delegate: Rectangle {
                                required property string modelData
                                width: 30; height: 30; radius: 15
                                color: card.c[modelData]
                                border { color: card.c.line; width: 1 }
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            win.deleting = ""
                            library.activate(card.modelData.id)
                        }
                        onPressAndHold: if (card.modelData.own) win.deleting = card.modelData.id
                    }

                    // holding one of the owner's: delete it?
                    Rectangle {
                        visible: win.deleting === card.modelData.id
                        anchors { right: parent.right; bottom: parent.bottom; margins: 14 }
                        width: deleteLabel.implicitWidth + 32; height: 44; radius: 22
                        color: deleteTap.pressed ? Qt.darker(Theme.red, 1.2) : Theme.red
                        Text {
                            id: deleteLabel
                            anchors.centerIn: parent
                            text: qsTr("Delete")
                            color: Theme.bg
                            font { family: Theme.font; pixelSize: 15; bold: true }
                        }
                        MouseArea {
                            id: deleteTap
                            anchors.fill: parent
                            onClicked: { library.remove(card.modelData.id); win.deleting = "" }
                        }
                    }
                }
            }

            // new ones: the agent makes them
            Text {
                width: parent.width
                topPadding: 8
                text: "󰚩  " + qsTr("New themes come from the agent. Ask it, for example: “make a theme in Pokémon style, with a wallpaper”.")
                color: Theme.muted
                wrapMode: Text.Wrap
                font { family: Theme.font; pixelSize: 14 }
            }
        }
    }
}
