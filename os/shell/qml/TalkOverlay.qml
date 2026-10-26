pragma ComponentBehavior: Bound

import QtQuick
import org.kde.layershell as LayerShell
import Mobarchy.Shell

// Frigg (ADR 0026, 0037): the conversation with the agent, live or in text (FriggView), a
// layer of its own over whatever is on the screen. Opened by the home board's Frigg card, or
// by "Hei Frigg" (then live). It keeps clear of the status bar and the keyboard.
LayerWindow {
    id: overlay

    required property Agent agent
    required property Talk talk
    required property FriggSettings settings
    required property Keyboard keyboard
    property bool locked: false
    signal copyOffered(string text)

    function openFrigg(how) { view.openWith(how) }
    function close() { if (view.open > 0) view.collapse() }

    // (not while Frigg uses the screen: she would see and tap her own overlay)
    shown: (view.open > 0 || talk.talking) && !talk.usingScreen
    color: "transparent"
    LayerShell.Window.scope: "mobarchy-talk"
    LayerShell.Window.layer: LayerShell.Window.LayerOverlay
    LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                               | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
    LayerShell.Window.exclusionZone: 0
    // (in text: the keyboard, for a hardware one's keys, FriggView.key)
    LayerShell.Window.keyboardInteractivity: view.keysWanted ? LayerShell.Window.KeyboardInteractivityExclusive
                                                             : LayerShell.Window.KeyboardInteractivityNone
    onActiveChanged: if (active) view.takeKeys()

    // "Hei Frigg" (or Frigg started from elsewhere): open, live
    Connections {
        target: overlay.talk
        function onStateChanged() {
            if (overlay.talk.talking && view.open === 0)
                view.openWith("live")
        }
    }

    FriggView {
        id: view
        anchors.fill: parent
        agent: overlay.agent
        talk: overlay.talk
        settings: overlay.settings
        keyboard: overlay.keyboard
        locked: overlay.locked
        onCopyOffered: (text) => overlay.copyOffered(text)
    }
}
