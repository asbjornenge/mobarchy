import QtCore
import QtQuick
import QtMultimedia
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
    // Frigg: the conversation with the agent, live or in text, over everything (ADR 0037)
    readonly property TalkOverlay talkOverlay: TalkOverlay {
        agent: shell.agent
        talk: shell.talk
        settings: shell.friggSettings
        keyboard: shell.keyboard
        locked: shell.lockScreen.shown
        onCopyOffered: (text) => shell.copyBar.offer(text)
    }
    readonly property Keyboard keyboard: Keyboard {}
    readonly property Clipboard clipboard: Clipboard {}
    readonly property CopyBar copyBar: CopyBar { clipboard: shell.clipboard; keyboard: shell.keyboard }
    readonly property KeyboardPanel keyboardPanel: KeyboardPanel {
        keyboard: shell.keyboard
        clipboard: shell.clipboard
        sideInsets: shell.leftInset.inset + shell.rightInset.inset
        laptop: shell.laptop
    }
    // Alt+Space on a hardware keyboard: the emoji, searched from its keys
    readonly property EmojiPicker emojiPicker: EmojiPicker {
        keyboard: shell.keyboard
        panel: shell.keyboardPanel
    }

    // --- laptop mode -----------------------------------------------------------------------
    // A foldable half open and turned so that its fold lies across (device.toml fold_axis):
    // the lower half is a keyboard (LaptopKeyboard, always up), the upper half the screen,
    // the lock screen too. From the hinge's angle (with some slack, so that it doesn't
    // flicker at the edges) and how the screen is turned.
    readonly property Hinge hinge: Hinge {
        active: shell.screen.on && shell.device.foldAxis(shell.inUse) !== ""
    }
    property bool laptop: false
    function updatePosture() {
        const axis = device.foldAxis(inUse)
        const across = axis === "vertical" ? rotation.turns % 2 === 1
                     : axis === "horizontal" ? rotation.turns % 2 === 0 : false
        const a = hinge.angle
        const open = laptop ? a >= 40 && a <= 165 : a >= 60 && a <= 150
        const want = across && a >= 0 && open
        if (want !== laptop)
            posture.restart()
        else
            posture.stop()
    }
    readonly property Timer posture: Timer {
        interval: 350       // (settled: not each step of a hinge on its way)
        onTriggered: shell.laptop = !shell.laptop
    }
    readonly property Connections postureFollows: Connections {
        target: shell.hinge
        function onAngleChanged() { shell.updatePosture() }
    }
    onInUseChanged: updatePosture()
    readonly property Connections postureTurns: Connections {
        target: shell.rotation
        function onTurnsChanged() { shell.updatePosture() }
    }
    // the lower half of the screen in use (logical px), the keyboard's with the edge under it
    readonly property real laptopBottom: statusBar.screen ? statusBar.screen.height / 2 : 0
    readonly property LaptopKeyboard laptopKeyboard: LaptopKeyboard {
        keyboard: shell.keyboard
        wanted: shell.laptop
        wantedHeight: Math.max(200, shell.laptopBottom - shell.edge.height)
        sideInsets: shell.leftInset.inset + shell.rightInset.inset
        // home's apps, searching (type its name, Enter starts it); again: back to the widgets
        onSuperTapped: {
            if (shell.lockScreen.shown)
                return
            if (shell.home.searchingApps) {
                shell.home.showWidgets()
            } else {
                shell.goHome()
                shell.home.searchApps()
            }
        }
    }
    // (followed while the screen is on: its reads woke a sleeping phone)
    readonly property SystemStatus system: SystemStatus { active: shell.screen.on }
    // The battery low: a word at 15 % and at 5 %, once each while it discharges (the power
    // off at 2 % is battery-guard's, which works with the screen off too).
    property int batteryWarnedAt: 101
    readonly property Connections batteryWatch: Connections {
        target: shell.system
        function onChanged() {
            const level = shell.system.battery
            if (level < 0 || shell.system.charging) {
                shell.batteryWarnedAt = 101
                return
            }
            for (const mark of [5, 15]) {
                if (level <= mark && shell.batteryWarnedAt > mark) {
                    shell.batteryWarnedAt = mark
                    shell.notifications.post(qsTr("Battery at %1 %").arg(level),
                        mark === 5 ? qsTr("The phone powers off at 2 %. Plug it in.") : qsTr("Plug it in soon."))
                    break
                }
            }
        }
    }
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
    // GeoClue's agent: which apps may know where the phone is (ADR 0033)
    readonly property LocationAgent locationAgent: LocationAgent {}
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
        onPowerPressed: shell.ringing && !shell.ringerSilenced ? shell.ringerSilenced = true
                      : shell.alarms.ringing ? shell.alarms.snooze()
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
    // Deep sleep (ADR 0043): allowed with the screen off and nothing that needs the phone
    // awake (a call, ringing, an alarm, something playing, Frigg in a conversation or
    // listening for "Hei Frigg", an app keeping the screen on); the next alarm or timer wakes it
    readonly property SleepGate sleepGate: SleepGate {
        displayOn: shell.screen.on
        allowed: !shell.screen.on && !shell.inCall && !shell.ringing && !shell.alarms.ringing
                 && !shell.media.playing && !shell.talk.talking && !shell.talk.wake
                 && !shell.inhibitors.active
        nextWake: {
            let next = shell.alarms.nextAlarm
            for (const t of shell.alarms.timerList) {
                const ends = new Date(t.ends)
                if (!next || isNaN(next.getTime()) || ends < next)
                    next = ends
            }
            return next
        }
    }
    // Screen off after a while without input (the owner's choice in quick settings; a minute
    // by default, never if they say so), unless an app keeps it on (Wayland idle inhibitors
    // count too: the compositor holds the notification back), or Frigg is in a conversation.
    readonly property Settings screenSettings: Settings {
        location: StandardPaths.writableLocation(StandardPaths.GenericStateLocation) + "/mobarchy/screen.ini"
        property int timeout: 60
    }
    readonly property Idle idle: Idle {
        timeout: shell.inhibitors.active || shell.talk.talking || shell.alarms.ringing || shell.ringing || shell.inCall
                 ? 0 : shell.screenSettings.timeout
        onIdleChanged: if (idle && shell.screen.on) shell.screenOff()
    }
    // A woken but untouched lock screen goes dark again soon.
    readonly property Timer lockTimeout: Timer {
        interval: 10000
        running: shell.lockScreen.shown && shell.screen.on && !shell.alarms.ringing && !shell.ringing
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

    // ⌘+← and ⌘+→ (a hardware keyboard: HomeKeys, from Hyprland's binds): three places
    // side by side, the notifications, what is open in the middle (the app in front, or the
    // widgets), the apps. From an app, home comes up with the app's picture in the middle and
    // slides to the side page, as from the widgets; back slides to the picture, then to the
    // app itself.
    readonly property HomeKeys homeKeys: HomeKeys {
        onLeft: shell.side(-1)
        onRight: shell.side(1)
    }
    property int sideTo: 1      // (the side page to slide to once home is in front)
    function side(step) {
        if (lockScreen.shown)
            return
        if (!home.active && toplevels.count > 0) {
            const front = toplevels.active()[0]
            if (!front)
                return
            switcher.pictureOf(front.appId, front.title)    // (a fresh one, of it alone)
            home.appPicture = "image://windows/" + encodeURIComponent(front.appId) + "/" + encodeURIComponent(front.title)
            sideTo = step < 0 ? 0 : 2
            bringHome(1)
            shell.slideAway.restart()
            return
        }
        const to = home.page + step
        if (to >= 0 && to <= 2)
            home.showPage(to, true)
    }
    // (home in front, the app's picture where the app was: then the slide, as soon as home
    // has the front; this if that is slow to come)
    readonly property Timer slideAway: Timer { interval: 150; onTriggered: shell.home.showPage(shell.sideTo, true) }
    // (back at the app's picture: the app, once the slide is over)
    readonly property Timer backToApp: Timer { interval: 260; onTriggered: shell.toplevels.activate(0) }
    readonly property Connections homeSides: Connections {
        target: shell.home
        function onAppReached() { shell.backToApp.restart() }
        // (home left, however: the middle is the widgets again)
        function onActiveChanged() {
            if (shell.home.active && shell.slideAway.running) {
                shell.slideAway.stop()
                shell.home.showPage(shell.sideTo, true)
            }
            if (!shell.home.active && !shell.slideAway.running) {
                shell.home.appPicture = ""
                shell.home.showWidgets()
            }
        }
    }
    // Home up on one of its pages, as it was left otherwise (not back to the widgets)
    function bringHome(page) {
        switcher.close()
        quick.close()
        talkOverlay.close()
        home.showPage(page, false)
        if (!home.visible)
            home.show()
        toplevels.activateOwn(home.title)
    }

    function goHome() {
        switcher.close()
        quick.close()
        keyboard.visible = false
        talkOverlay.close()     // home is the widgets: the conversation closes
        home.showWidgets()
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
        keyboard: shell.keyboard
        laptop: shell.laptop
        laptopBottom: shell.laptopBottom
        onUnlocked: {
            shown = false
            // Focus goes back to the app behind; the keyboard should not come up with it.
            shell.keyboard.visible = false
        }
        onTouched: shell.lockTimeout.restart()
        // a notification tapped on the lock screen: opened once unlocked
        onOpenNotification: (id) => shell.notifications.invoke(id, "default")
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
    // flight mode and the mobile network switch, kept across restarts. Flight mode is a state
    // of its own (as on other phones): it takes the mobile radio, Wi-Fi and Bluetooth down,
    // and Wi-Fi or Bluetooth can be turned on again during it; the mobile radio stays off.
    readonly property Settings radioSettings: Settings {
        location: StandardPaths.writableLocation(StandardPaths.GenericStateLocation) + "/mobarchy/radio.ini"
        property bool flight: false
        property bool mobile: true
    }
    readonly property Telephony telephony: Telephony {
        keeper: true
        radio: !shell.radioSettings.flight && shell.radioSettings.mobile
        onIncomingCall: {
            shell.wake()
            if (!shell.toplevels.activateApp(shell.apps.windowIds("org.mobarchy.Phone")))
                shell.apps.launch("org.mobarchy.Phone")
        }
        // a call going out (from Contacts, or Frigg): the Phone app shows it, once a call
        property var shownCalls: []
        // missed: a call that came in and went without being answered (from whom, by path)
        property var unanswered: ({})
        onCallsChanged: {
            const now = {}
            for (const c of calls)
                now[c.path] = c
            for (const path in unanswered) {
                if (!now[path])
                    shell.missedCall(unanswered[path])
            }
            const next = {}
            for (const c of calls) {
                if (c.state === "incoming" || c.state === "waiting")
                    next[c.path] = c.number
            }
            unanswered = next
            const out = calls.filter(c => (c.state === "dialing" || c.state === "alerting")
                                          && !shownCalls.includes(c.path))
            shownCalls = calls.map(c => c.path)
            if (out.length && !shell.toplevels.active().some(w => w.appId === "org.mobarchy.Phone")
                    && !shell.toplevels.activateApp(shell.apps.windowIds("org.mobarchy.Phone")))
                shell.apps.launch("org.mobarchy.Phone")
        }
    }

    // A call at the ear: the proximity sensor, while a call is up on the earpiece; near, the
    // screen goes black and takes no touches (CallShade). No screen timeout during a call.
    readonly property bool inCall: telephony.calls.some(c => c.state === "active" || c.state === "dialing"
                                                         || c.state === "alerting" || c.state === "held")
    readonly property Proximity proximity: Proximity { active: shell.inCall && !shell.telephony.speaker }
    readonly property CallShade callShade: CallShade { shown: shell.proximity.near }

    // A call coming in rings (the ringtone, over and over, and a buzz) until it is answered,
    // declined or gone; the power key silences it. Muted: only the buzz.
    readonly property bool ringing: telephony.calls.some(c => c.state === "incoming" || c.state === "waiting")
    property bool ringerSilenced: false
    onRingingChanged: {
        ringerSilenced = false
        if (ringing && !volume.muted && !telephony.calls.some(c => c.state === "active")) {
            ringtone.play()
        } else {
            ringtone.stop()
        }
    }
    onRingerSilencedChanged: if (ringerSilenced) ringtone.stop()
    readonly property MediaPlayer ringtone: MediaPlayer {
        source: "qrc:/qt/qml/Mobarchy/Shell/data/sounds/ringtone.wav"
        loops: MediaPlayer.Infinite
        audioOutput: AudioOutput { volume: 1.0 }
    }
    readonly property Timer ringBuzz: Timer {
        interval: 1500; repeat: true; triggeredOnStart: true
        running: shell.ringing && !shell.ringerSilenced
        onTriggered: shell.haptics.buzz()
    }

    // A missed call: a notification (the caller by name, if a contact); a tap opens Phone.
    property var missedNotes: ({})
    function missedCall(number) {
        const id = notifications.post(qsTr("Missed call"), contacts.nameFor(number) || number || qsTr("Unknown number"),
                                      "org.mobarchy.Phone")
        const notes = missedNotes
        notes[id] = number
        missedNotes = notes
    }
    readonly property Connections missedTapped: Connections {
        target: shell.notifications
        function onInvoked(id, action) {
            if (shell.missedNotes[id] === undefined)
                return
            delete shell.missedNotes[id]
            if (!shell.toplevels.activateApp(shell.apps.windowIds("org.mobarchy.Phone")))
                shell.apps.launch("org.mobarchy.Phone")
        }
    }

    // A text message: a notification with who and what (the sender by name, if a contact),
    // a tone and a buzz, and the lock screen lit for a glance; a tap opens the conversation.
    readonly property Contacts contacts: Contacts {}
    property var messageNotes: ({})     // notification id -> the sender's number
    readonly property Connections incomingMessage: Connections {
        target: shell.telephony
        function onMessageReceived(from, text) {
            const id = shell.notifications.post(shell.contacts.nameFor(from) || from, text, "org.mobarchy.Messages")
            const notes = shell.messageNotes
            notes[id] = from
            shell.messageNotes = notes
            shell.haptics.buzz()
            if (!shell.volume.muted && !shell.alarms.ringing && shell.telephony.calls.length === 0) {
                messageTone.stop()
                messageTone.play()
            }
            if (!shell.screen.on)
                shell.wake()
        }
    }
    readonly property Connections messageTapped: Connections {
        target: shell.notifications
        function onInvoked(id, action) {
            const number = shell.messageNotes[id]
            if (number === undefined)
                return
            delete shell.messageNotes[id]
            Messaging.open(number)
        }
    }
    // An app's notification tapped: the app, at what the notification was about (the shell's
    // own apps: Mail at the message), else the app's window or the app started.
    readonly property Connections notificationOpened: Connections {
        target: shell.notifications
        function onOpened(entry, target) {
            const id = entry.replace(/\.desktop$/, "")
            const own = ({ "org.mobarchy.Mail": "--mail", "org.mobarchy.Messages": "--messages" })[id]
            if (own)
                AppLinks.open(id, own, target)
            else if (!shell.toplevels.activateApp(shell.apps.windowIds(id)))
                shell.apps.launch(id)
        }
    }
    readonly property MediaPlayer messageTone: MediaPlayer {
        source: "qrc:/qt/qml/Mobarchy/Shell/data/sounds/message.wav"
        audioOutput: AudioOutput { volume: 0.8 }
    }

    readonly property SimPinScreen simPin: SimPinScreen {
        telephony: shell.telephony
        onShownChanged: if (shown) shell.wake()
    }

    readonly property StatusBar statusBar: StatusBar {
        system: shell.system
        telephony: shell.telephony
        flight: shell.radioSettings.flight
        quick: shell.quick
        device: shell.device
        notifications: shell.notifications
        listening: shell.talk.active
        alarmSet: !isNaN(shell.alarms.nextAlarm.getTime())
        turns: shell.rotation.turns
    }
    readonly property Home home: Home {
        talk: shell.talk
        media: shell.media
        alarms: shell.alarms
        telephony: shell.telephony
        system: shell.system
        apps: shell.apps
        toplevels: shell.toplevels
        notifications: shell.notifications
        keyboard: shell.keyboard
        pictureGeneration: shell.switcher.pictureGeneration
        screenOn: shell.screen.on
        onFriggAsked: shell.talkOverlay.openFrigg("live")
    }
    readonly property QuickSettings quick: QuickSettings {
        brightness: shell.brightness
        keyboard: shell.keyboard
        system: shell.system
        power: shell.power
        wifi: shell.wifi
        rfkill: shell.rfkill
        telephony: shell.telephony
        radioSettings: shell.radioSettings
        screenSettings: shell.screenSettings
        bluetooth: shell.bluetooth
        media: shell.media
        pinLock: shell.pinLock
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
        switcher: shell.switcher
        keyboard: shell.keyboard
        laptop: shell.laptop
        inset: shell.device.inset(shell.inUse, "bottom", shell.rotation.turns)
        onHome: shell.goHome()
    }
}
