pragma ComponentBehavior: Bound

import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Frigg (ADR 0026): the live conversation, over whatever is on the screen (FriggStage): a
// conversation "Hei Frigg" started away from home. At home the conversation shows her itself.
// ✕ ends the conversation; "Aa" goes on in text, at home.
LayerWindow {
    id: overlay

    required property Talk talk
    required property FriggSettings settings
    property bool locked: false
    property int insetBottom: 0
    // the conversation at home shows her itself (open, live, in front)
    property bool hosted: false
    // "Aa": on in text, at home
    signal textAsked()

    // (not while Frigg uses the screen: she would see and tap her own overlay)
    shown: talk.talking && !talk.usingScreen && !hosted
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-talk"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: -1
    LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone

    Rectangle {
        anchors.fill: parent
        color: "#c0000000"
        MouseArea { anchors.fill: parent }
    }

    FriggStage {
        anchors.fill: parent
        talk: overlay.talk
        settings: overlay.settings
        locked: overlay.locked
        bottomInset: overlay.insetBottom
        offerText: !overlay.locked
        visible: overlay.shown
        onCloseAsked: overlay.talk.stop()
        onTextAsked: overlay.textAsked()
    }
}
