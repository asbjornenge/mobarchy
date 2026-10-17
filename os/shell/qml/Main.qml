import QtQuick
import Mobarchy.Shell

QtObject {
    id: shell

    readonly property AppModel apps: AppModel {}
    readonly property ToplevelModel toplevels: ToplevelModel {}
    readonly property Agent agent: Agent {}
    // Frigg, the live voice conversation (ADR 0026). "Hei Frigg" starts one too, when the
    // owner has it on: always, or while charging. On a locked phone Frigg answers the owner's
    // voice as the owner, and anyone else with the harmless tools only (Voice Match).
    readonly property Talk talk: Talk {
        wake: shell.friggSettings.wake === "always"
              || shell.friggSettings.wake === "charging" && shell.system.charging
    }
    readonly property FriggSettings friggSettings: FriggSettings {}
    // What was said to Frigg is in the chat's conversation: the chat shows it after each
    // answer.
    readonly property Connections friggToChat: Connections {
        target: shell.talk
        function onStateChanged() {
            if (shell.talk.state === "listening" || shell.talk.state === "waiting" || shell.talk.state === "idle")
                shell.agent.loadConversation()
        }
    }
    readonly property TalkOverlay talkOverlay: TalkOverlay {
        talk: shell.talk
        settings: shell.friggSettings
        locked: shell.lockScreen.shown
        insetBottom: shell.edge.inset
    }
    readonly property Keyboard keyboard: Keyboard {}
    readonly property Clipboard clipboard: Clipboard {}
    readonly property CopyBar copyBar: CopyBar { clipboard: shell.clipboard; keyboard: shell.keyboard }
    readonly property KeyboardPanel keyboardPanel: KeyboardPanel {
        keyboard: shell.keyboard
        clipboard: shell.clipboard
        sideInsets: shell.leftInset.inset + shell.rightInset.inset
    }
    readonly property SystemStatus system: SystemStatus {}
    readonly property Brightness brightness: Brightness {}
    readonly property Power power: Power {}
    readonly property Device device: Device {}
    readonly property Wifi wifi: Wifi {}
    // A Wi-Fi network's login page (a captive portal: cafés, airports): the browser opens it
    // (after the unlock, if locked), and a notification does too until signed in.
    property int portalNote: 0
    property bool portalPending: false
    readonly property Portal portal: Portal {
        network: shell.wifi.state === "COMPLETED" ? shell.wifi.ssid : ""
        onPortalChanged: {
            if (portal) {
                shell.portalNote = shell.notifications.post(qsTr("Sign in to %1").arg(network),
                        qsTr("This Wi-Fi network wants you to sign in. Tap to open its page."))
                shell.portalPending = shell.lockScreen.shown
                if (!shell.portalPending)
                    Qt.openUrlExternally(url)
            } else {
                if (shell.portalNote)
                    shell.notifications.dismiss(shell.portalNote)
                shell.portalNote = 0
                shell.portalPending = false
            }
        }
    }
    readonly property Connections portalTapped: Connections {
        target: shell.notifications
        function onInvoked(id, action) {
            if (id === shell.portalNote && shell.portal.portal)
                Qt.openUrlExternally(shell.portal.url)
        }
    }
    readonly property Connections portalUnlock: Connections {
        target: shell.lockScreen
        function onShownChanged() {
            if (!shell.lockScreen.shown && shell.portalPending) {
                shell.portalPending = false
                Qt.openUrlExternally(shell.portal.url)
            }
        }
    }
    readonly property Rfkill rfkill: Rfkill {}
    readonly property Bluetooth bluetooth: Bluetooth {}
    readonly property MediaPlayers media: MediaPlayers {}
    readonly property Torch torch: Torch {}
    readonly property Haptics haptics: Haptics {}
    readonly property Fingerprint fingerprint: Fingerprint {}
    readonly property Rotation rotation: Rotation {
        device: shell.device
        screen: shell.screen
    }
    readonly property PinLock pinLock: PinLock {}
    // The agent may not use the screen of a phone locked with a PIN (mobarchy-mcp checks).
    readonly property bool lockedWithPin: lockScreen.shown && pinLock.hasPin
    onLockedWithPinChanged: pinLock.setLocked(lockedWithPin)
    readonly property Volume volume: Volume {}
    readonly property Notifications notifications: Notifications {}
    readonly property Banner banner: Banner {
        notifications: shell.notifications
        suppressed: shell.lockScreen.shown || shell.quick.shown
    }

    // --- screen, power key, lock -----------------------------------------------------
    readonly property ScreenPower screen: ScreenPower {
        // Another panel came up (a fold): wake up, as phones do when they are opened.
        onScreensChanged: shell.wake()
    }
    readonly property HardwareKeys keys: HardwareKeys {
        // (an alarm ringing: the power key snoozes it, as on other phones)
        onPowerPressed: shell.alarms.ringing ? shell.alarms.snooze()
                                             : shell.screen.on ? shell.screenOff() : shell.wake()
        // A tap on the dark screen (where the touchscreen supports it) shows the lock
        // screen for a glance: the clock and notifications, dark again after a while.
        onWakeGesture: if (!shell.screen.on) shell.wake()
        onVolumeUp: shell.stepVolume(+1)
        onVolumeDown: shell.stepVolume(-1)
        // Media keys (a headset's buttons) go to the player in charge.
        onMediaPlayPause: shell.media.playPause()
        onMediaPause: shell.media.pause()
        onMediaNext: shell.media.next()
        onMediaPrevious: shell.media.previous()
        onScreenshot: shell.screenshot.take()
        onPowerLongPressed: {
            shell.wake()
            if (!shell.lockScreen.shown)
                shell.quick.open()
        }
    }
    // Apps that keep the screen on (a playing video) through org.freedesktop.ScreenSaver.
    readonly property Inhibitors inhibitors: Inhibitors {}
    // Screen off after a minute without input, unless an app keeps it on (Wayland idle
    // inhibitors count too: the compositor holds the notification back), or Frigg is in a
    // conversation.
    readonly property Idle idle: Idle {
        timeout: shell.inhibitors.active || shell.talk.talking || shell.alarms.ringing ? 0 : 60
        onIdleChanged: if (idle && shell.screen.on) shell.screenOff()
    }
    // A woken but untouched lock screen goes dark again soon.
    readonly property Timer lockTimeout: Timer {
        interval: 10000
        running: shell.lockScreen.shown && shell.screen.on && !shell.alarms.ringing
        // (not while an alarm rings: wake() restarts this, which ends the binding above)
        onTriggered: if (shell.lockScreen.shown && !shell.alarms.ringing) shell.screen.on = false
    }

    // Volume keys: 5 % steps; the bar shows only when the screen is on.
    function stepVolume(direction) {
        if (!volume.available)
            return
        volume.volume = Math.max(0, Math.min(1, Math.round(volume.volume * 20 + direction) / 20))
        if (screen.on)
            volumeOsd.flash()
    }
    readonly property VolumeOsd volumeOsd: VolumeOsd { volume: shell.volume }

    // A phone with a PIN starts locked: until the PIN is given since it started, and when the
    // shell restarts on a locked phone (a restart in between on an open one keeps it open).
    function lockAtStart() {
        if (pinLock.hasPin && (!pinLock.givenSinceBoot || pinLock.wasLocked))
            lock()
    }
    readonly property Connections hasPinKnown: Connections {
        target: shell.pinLock
        function onHasPinChanged() { shell.lockAtStart() }
    }
    Component.onCompleted: lockAtStart()

    function lock() {
        drawer.close()
        quick.close()
        keyboard.visible = false
        lockScreen.shown = true
    }
    function screenOff() {
        lock()
        screen.on = false
    }
    // Touch points whose "up" got lost would make the shell ignore every tap and swipe.
    readonly property TouchGuard touchGuard: TouchGuard {}

    function wake() {
        // No finger is on the glass while the screen is dark: anything Qt still holds is stale.
        if (!screen.on)
            touchGuard.reset("wake")
        screen.on = true
        // Only the lock screen goes dark again by itself. (A fold wakes an unlocked phone
        // too: restarting the timer then turned the screen off 10 s later, video or not.)
        if (lockScreen.shown && !alarms.ringing)
            lockTimeout.restart()
    }

    function goHome() {
        drawer.close()
        switcher.close()
        quick.close()
        keyboard.visible = false
        if (!home.visible)
            home.show()
        toplevels.activateOwn(home.title)
    }

    readonly property LockScreen lockScreen: LockScreen {
        system: shell.system
        notifications: shell.notifications
        media: shell.media
        pinLock: shell.pinLock
        torch: shell.torch
        haptics: shell.haptics
        fingerprint: shell.fingerprint
        insetLeft: shell.leftInset.inset
        insetRight: shell.rightInset.inset
        insetBottom: shell.edge.inset
        nextAlarm: shell.alarms.nextAlarm
        onUnlocked: {
            shown = false
            // Focus goes back to the app behind; the keyboard should not come up with it.
            shell.keyboard.visible = false
        }
        onTouched: shell.lockTimeout.restart()
        // a finger unlocks a dark screen too: it comes on
        onFingerUnlocked: shell.screen.on = true
    }

    // Power and volume down together: a screenshot, saved; a flash and a click when it is.
    readonly property Screenshot screenshot: Screenshot {
        onTaken: {
            shell.haptics.click()
            shell.flash.go()
        }
    }
    readonly property ScreenFlash flash: ScreenFlash {}

    // Alarms and timers (ADR 0028): ringing wakes the screen, over the lock screen.
    readonly property Alarms alarms: Alarms {
        onStarted: shell.wake()
        // stopped on the lock screen: dark again soon, as after any glance
        onRingingChanged: if (!ringing && shell.lockScreen.shown && shell.screen.on) shell.lockTimeout.restart()
    }
    readonly property AlarmScreen alarmScreen: AlarmScreen {
        alarms: shell.alarms
        haptics: shell.haptics
        volume: shell.volume
    }

    // Calls and SMS (ADR 0031): the shell's own instance keeps incoming SMS, and a ringing
    // call brings up the Phone app (and the screen).
    readonly property Telephony telephony: Telephony {
        keeper: true
        onIncomingCall: { shell.wake(); shell.apps.launch("org.mobarchy.Phone") }
    }

    readonly property StatusBar statusBar: StatusBar {
        system: shell.system
        telephony: shell.telephony
        quick: shell.quick
        device: shell.device
        notifications: shell.notifications
        listening: shell.talk.active
        alarmSet: !isNaN(shell.alarms.nextAlarm.getTime())
        turns: shell.rotation.turns
    }
    readonly property Home home: Home {
        agent: shell.agent
        talk: shell.talk
        keyboard: shell.keyboard
        onCopyOffered: (text) => shell.copyBar.offer(text)
    }
    readonly property Drawer drawer: Drawer {
        apps: shell.apps
        toplevels: shell.toplevels
        keyboard: shell.keyboard
        onHome: shell.goHome()
    }
    readonly property QuickSettings quick: QuickSettings {
        brightness: shell.brightness
        keyboard: shell.keyboard
        system: shell.system
        power: shell.power
        wifi: shell.wifi
        rfkill: shell.rfkill
        bluetooth: shell.bluetooth
        media: shell.media
        pinLock: shell.pinLock
        notifications: shell.notifications
        volume: shell.volume
        apps: shell.apps
        rotation: shell.rotation
        torch: shell.torch
        haptics: shell.haptics
        fingerprint: shell.fingerprint
        onPassphraseNeeded: (ssid) => {
            shell.quick.close()
            shell.wifiPassword.ask(ssid)
        }
    }
    readonly property WifiPassword wifiPassword: WifiPassword { wifi: shell.wifi }
    // The owner approves the agent's risky steps, and root, on the screen (ADR 0024). A
    // locked phone turns the agent down; polkit's requests wait for the unlock.
    readonly property Approvals approvals: Approvals { locked: shell.lockScreen.shown }
    readonly property ApprovalSheet approvalSheet: ApprovalSheet {
        approvals: shell.approvals
        haptics: shell.haptics
    }
    readonly property PolkitAgent polkit: PolkitAgent { held: shell.lockScreen.shown }
    readonly property AuthSheet authSheet: AuthSheet {
        polkit: shell.polkit
        haptics: shell.haptics
    }
    // One sheet at a time.
    readonly property Connections drawerOpens: Connections {
        target: shell.drawer
        function onOpening() { shell.quick.close() }
    }
    readonly property Connections quickOpens: Connections {
        target: shell.quick
        function onOpening() { shell.drawer.close() }
    }
    // The screen in use (the status bar follows it) and its insets at the other edges, as
    // it is turned. The strips are arranged first (bottom layer), so the bars and windows
    // all sit inside them.
    readonly property string inUse: statusBar.screen ? statusBar.screen.name : ""
    readonly property SideInset leftInset: SideInset {
        edge: "left"
        inset: shell.device.inset(shell.inUse, "left", shell.rotation.turns)
    }
    readonly property SideInset rightInset: SideInset {
        edge: "right"
        inset: shell.device.inset(shell.inUse, "right", shell.rotation.turns)
    }
    // (the bottom inset belongs to the edge: a swipe can start on it)
    readonly property AppSwitcher switcher: AppSwitcher {
        toplevels: shell.toplevels
        apps: shell.apps
        haptics: shell.haptics
        insetBottom: shell.edge.inset
    }
    readonly property Edge edge: Edge {
        drawer: shell.drawer
        switcher: shell.switcher
        keyboard: shell.keyboard
        inset: shell.device.inset(shell.inUse, "bottom", shell.rotation.turns)
        onHome: shell.goHome()
    }
}
