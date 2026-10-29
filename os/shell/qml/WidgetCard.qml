pragma ComponentBehavior: Bound

import QtQuick
import Mobarchy.Shell

// One of the owner's own widgets (ADR 0046): a description in home.json's "widgets", drawn
// here with one of four templates from the content mobarchy-widgets fetched for it. Nothing
// of it runs: a list (a tap opens an item's link), a value, a text (markdown) or an image.
BoardCard {
    id: card

    // home.json's description: {title, icon, template, source, board}
    property var spec: ({})
    // as last fetched: {items | value, caption | text | image, updated, error} (not `content`:
    // that is the card's own, what it shows)
    property var fetched: ({})
    // (for its markdown)
    property WidgetContents contents: null

    readonly property string template: spec.template || "list"
    // its own look for its text, if it has one: {font, size, italic, align} (a quote)
    readonly property var style: spec.style || ({})
    readonly property string fontFamily: style.font || Theme.font
    readonly property int textAlign: style.align === "center" ? Text.AlignHCenter
                                   : style.align === "right" ? Text.AlignRight : Text.AlignLeft
    readonly property var items: fetched.items ? Array.from(fetched.items) : []
    readonly property bool empty: template === "list" ? items.length === 0
                                : template === "value" ? !fetched.value
                                : template === "text" ? !fetched.text : !fetched.image

    title: spec.title || ""
    icon: spec.icon || ""
    compactIcon: icon
    compactValue: fetched.value || "–"
    compactCaption: fetched.caption || title

    // (a link opens only if it is the web's)
    function open(url) {
        if (/^https?:\/\//.test(url || ""))
            Qt.openUrlExternally(url)
    }

    // nothing yet: fetching, or why not
    Text {
        visible: card.empty
        anchors.centerIn: parent
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        maximumLineCount: 3
        elide: Text.ElideRight
        text: card.fetched.error ? "󰀦  " + card.fetched.error : qsTr("Fetching…")
        color: Theme.muted
        font { family: Theme.font; pixelSize: 12 }
    }

    // --- a list ------------------------------------------------------------------------------
    ListView {
        id: list
        visible: card.template === "list" && !card.empty
        anchors.fill: parent
        clip: true
        spacing: 10
        interactive: contentHeight > height && !card.editing
        boundsBehavior: Flickable.StopAtBounds
        model: card.template === "list" ? card.items : []
        delegate: Item {
            id: row
            required property var modelData
            width: list.width
            height: Math.max(text.implicitHeight, thumb.visible ? 48 : 0)
            Image {
                id: thumb
                visible: row.modelData.image !== "" && status === Image.Ready
                anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                width: 64; height: 48
                source: row.modelData.image ? "file://" + row.modelData.image : ""
                sourceSize { width: 128; height: 96 }
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Column {
                id: text
                anchors { left: parent.left; right: thumb.visible ? thumb.left : parent.right
                          rightMargin: thumb.visible ? 10 : 0; verticalCenter: parent.verticalCenter }
                spacing: 2
                Text {
                    width: parent.width
                    text: row.modelData.title
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    maximumLineCount: card.cols >= 4 ? 2 : 3
                    elide: Text.ElideRight
                    font { family: card.fontFamily; pixelSize: card.style.size || 14; italic: !!card.style.italic }
                }
                Text {
                    visible: row.modelData.subtitle !== "" && card.cols >= 4
                    width: parent.width
                    text: row.modelData.subtitle
                    color: Theme.muted
                    elide: Text.ElideRight
                    font { family: Theme.font; pixelSize: 12 }
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: row.modelData.url !== ""
                onClicked: card.open(row.modelData.url)
                onPressAndHold: card.held()
            }
        }
    }

    // --- a value (compact: the card's own line) -----------------------------------------------
    Column {
        visible: card.template === "value" && !card.empty
        anchors.centerIn: parent
        width: parent.width
        spacing: 4
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: card.fetched.value || ""
            color: Theme.fg
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 18
            font { family: card.fontFamily; pixelSize: 44; weight: Font.Light; italic: !!card.style.italic }
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text !== ""
            text: card.fetched.caption || ""
            color: Theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 13 }
        }
    }

    // --- a text (and under it, who said it) ----------------------------------------------------
    Flickable {
        id: textFlick
        visible: card.template === "text" && !card.empty
        anchors.fill: parent
        clip: true
        contentHeight: textColumn.height
        interactive: contentHeight > height && !card.editing
        boundsBehavior: Flickable.StopAtBounds
        Column {
            id: textColumn
            width: parent.width
            // (centred, a short text sits in the middle of the card: a quote)
            y: card.style.align === "center" ? Math.max(0, (textFlick.height - height) / 2) : 0
            spacing: 8
            Text {
                id: markdown
                width: parent.width
                font { family: card.fontFamily; pixelSize: card.style.size || 14; italic: !!card.style.italic }
                text: card.template === "text" && card.fetched.text && card.contents
                      ? card.contents.markdown(card.fetched.text, font, Theme.accent) : ""
                textFormat: Text.RichText
                horizontalAlignment: card.textAlign
                wrapMode: Text.Wrap
                color: Theme.fg
                onLinkActivated: (url) => card.open(url)
            }
            Text {
                visible: !!card.fetched.caption
                width: parent.width
                horizontalAlignment: card.style.align === "center" ? Text.AlignHCenter : Text.AlignRight
                text: "— " + (card.fetched.caption || "")
                color: Theme.muted
                elide: Text.ElideLeft
                font { family: card.fontFamily; pixelSize: Math.max(12, Math.round((card.style.size || 14) * 0.7)) }
            }
        }
    }

    // --- an image ------------------------------------------------------------------------------
    Image {
        visible: card.template === "image" && !card.empty
        anchors.fill: parent
        // (the same file for each new picture: the time it was fetched tells them apart)
        source: card.template === "image" && card.fetched.image ? "file://" + card.fetched.image + "?" + card.fetched.updated : ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        // (a webcam's still: the same file name for a new picture)
        cache: false
        Text {
            visible: text !== ""
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
            text: card.fetched.caption || ""
            color: "white"
            style: Text.Outline
            styleColor: "#80000000"
            elide: Text.ElideRight
            font { family: Theme.font; pixelSize: 12 }
        }
    }
}
