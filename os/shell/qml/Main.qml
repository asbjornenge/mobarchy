import QtQuick
import Mobarchy.Shell

QtObject {
    id: shell

    readonly property AppModel apps: AppModel {}
    readonly property ToplevelModel toplevels: ToplevelModel {}
    readonly property Agent agent: Agent {}
    readonly property Keyboard keyboard: Keyboard {}
    readonly property SystemStatus system: SystemStatus {}
    readonly property Brightness brightness: Brightness {}
    readonly property Power power: Power {}
    readonly property Device device: Device {}
    readonly property Wifi wifi: Wifi {}
    readonly property Rfkill rfkill: Rfkill {}
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
    readonly property PowerButton powerButton: PowerButton {
        onPressed: shell.screen.on ? shell.screenOff() : shell.wake()
        onLongPressed: {
            shell.wake()
            if (!shell.lockScreen.shown)
                shell.quick.open()
        }
    }
    // Screen off after a minute without input (apps that play video inhibit this).
    readonly property Idle idle: Idle {
        timeout: 60
        onIdleChanged: if (idle && shell.screen.on) shell.screenOff()
    }
    // A woken but untouched lock screen goes dark again soon.
    readonly property Timer lockTimeout: Timer {
        interval: 10000
        running: shell.lockScreen.shown && shell.screen.on
        onTriggered: shell.screen.on = false
    }

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
    function wake() {
        screen.on = true
        lockTimeout.restart()
    }

    function goHome() {
        drawer.close()
        quick.close()
        keyboard.visible = false
        if (!home.visible)
            home.show()
        toplevels.activateOwn(home.title)
    }

    readonly property LockScreen lockScreen: LockScreen {
        system: shell.system
        notifications: shell.notifications
        onUnlocked: {
            shown = false
            // Focus goes back to the app behind; the keyboard should not come up with it.
            shell.keyboard.visible = false
        }
        onTouched: shell.lockTimeout.restart()
    }

    readonly property StatusBar statusBar: StatusBar {
        system: shell.system
        quick: shell.quick
        device: shell.device
        notifications: shell.notifications
    }
    readonly property Home home: Home { agent: shell.agent; keyboard: shell.keyboard }
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
        notifications: shell.notifications
        onPassphraseNeeded: (ssid) => {
            shell.quick.close()
            shell.wifiPassword.ask(ssid)
        }
    }
    readonly property WifiPassword wifiPassword: WifiPassword { wifi: shell.wifi }
    // One sheet at a time.
    readonly property Connections drawerOpens: Connections {
        target: shell.drawer
        function onOpening() { shell.quick.close() }
    }
    readonly property Connections quickOpens: Connections {
        target: shell.quick
        function onOpening() { shell.drawer.close() }
    }
    // The screen in use (the status bar follows it) and its side inset. The strips are
    // arranged first (bottom layer), so the bars and windows all sit inside them.
    readonly property int sideInset: device.sideInset(statusBar.screen ? statusBar.screen.name : "")
    readonly property SideInset leftInset: SideInset { left: true; inset: shell.sideInset }
    readonly property SideInset rightInset: SideInset { left: false; inset: shell.sideInset }
    readonly property Edge edge: Edge {
        drawer: shell.drawer
        keyboard: shell.keyboard
        onHome: shell.goHome()
    }
}
