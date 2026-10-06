pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Window
import QtWebEngine
import Mobarchy.Browser

// Mobarchy's browser (ADR 0023): the page, with one bar at the bottom for the thumb; the
// address, the tabs and the menu open as sheets over it.
Window {
    id: win
    width: 450; height: 900
    visible: true
    color: Theme.bg
    title: current && current.title ? current.title : qsTr("Browser")

    property var tabs: []
    property int index: 0
    readonly property var current: tabs.length > index ? tabs[index] : null
    property bool desktopSite: false
    property bool barShown: true
    readonly property string searchUrl: "https://duckduckgo.com/?q="
    readonly property string homeUrl: "https://duckduckgo.com/"

    // --- the profile: a phone's -------------------------------------------------------
    // QtWebEngine says "X11; Linux ..." and "QtWebEngine/x.y": sites send their desktop
    // versions. Chromium on Android is what the engine is.
    function mobileAgent(ua) {
        return ua.replace(/\([^)]*Linux[^)]*\)/, "(Linux; Android 15; Mobarchy)")
                 .replace(/ QtWebEngine\/[\d.]+/, "")
                 .replace(/ Safari\//, " Mobile Safari/")
    }
    function desktopAgent(ua) {
        return ua.replace(/ QtWebEngine\/[\d.]+/, "")
    }
    property string defaultAgent
    WebEngineProfile {
        id: profile
        storageName: "default"
        offTheRecord: false
        persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
        httpCacheType: WebEngineProfile.DiskHttpCache
        onDownloadRequested: (download) => {
            download.downloadDirectory = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                .toString().replace("file://", "")
            download.accept()
            toast.show(qsTr("Downloading %1").arg(download.downloadFileName))
        }
    }
    function applyAgent() {
        profile.httpUserAgent = desktopSite ? desktopAgent(defaultAgent) : mobileAgent(defaultAgent)
    }

    // What was visited, for the address sheet's suggestions (newest first, a few hundred).
    Settings {
        id: saved
        category: "history"
        property var visits: []
    }
    function remember(url, title) {
        const u = url.toString()
        if (!u.startsWith("http"))
            return
        const rest = saved.visits.filter(v => v.url !== u)
        saved.visits = [{url: u, title: title}].concat(rest).slice(0, 300)
    }

    // --- tabs --------------------------------------------------------------------------
    Component {
        id: tabComponent
        WebEngineView {
            id: view
            anchors { left: parent.left; right: parent.right; top: parent.top; bottom: parent.bottom
                      bottomMargin: win.barShown && !win.fullScreen ? bar.height : 0 }
            visible: win.current === view
            profile: profile
            settings.scrollAnimatorEnabled: true
            settings.fullScreenSupportEnabled: true
            settings.touchIconsEnabled: true
            property real lastY: 0
            onScrollPositionChanged: {
                if (view !== win.current)
                    return
                const y = scrollPosition.y
                if (y < 40 || y < lastY - 12)
                    win.barShown = true
                else if (y > lastY + 12)
                    win.barShown = false
                if (Math.abs(y - lastY) > 12)
                    lastY = y
            }
            onLoadingChanged: (info) => {
                if (info.status === WebEngineView.LoadSucceededStatus) {
                    win.remember(view.url, view.title)
                    win.checkLogin()
                }
                win.publish()
            }
            onTitleChanged: win.publish()
            onNewWindowRequested: (request) => {
                const t = win.newTab("")
                request.openIn(t)
            }
            onFullScreenRequested: (request) => {
                request.accept()
                win.fullScreen = request.toggleOn
                win.visibility = request.toggleOn ? Window.FullScreen : Window.AutomaticVisibility
            }
            onPermissionRequested: (permission) => permissionSheet.ask(view, permission)
            onWindowCloseRequested: win.closeTab(win.tabs.indexOf(view))
        }
    }
    property bool fullScreen: false

    function newTab(url) {
        const t = tabComponent.createObject(page, {})
        if (url)
            t.url = url
        tabs = tabs.concat([t])
        index = tabs.length - 1
        barShown = true
        publish()
        return t
    }
    function closeTab(i) {
        if (i < 0 || i >= tabs.length)
            return
        const t = tabs[i]
        const rest = tabs.slice()
        rest.splice(i, 1)
        tabs = rest
        t.destroy()
        if (!tabs.length)
            newTab(homeUrl)
        index = Math.min(index, tabs.length - 1)
        publish()
    }
    function load(text) {
        const t = text.trim()
        let url
        if (/^[a-z]+:\/\//i.test(t) || t.startsWith("about:"))
            url = t
        else if (!/\s/.test(t) && /^[^\/]+\.[a-z]{2,}(:\d+)?(\/.*)?$/i.test(t))
            url = "https://" + t
        else
            url = searchUrl + encodeURIComponent(t)
        if (current)
            current.url = url
        else
            newTab(url)
        barShown = true
    }
    // --- logins (1Password) ---------------------------------------------------------------
    // Whether the page in front asks for a login (a password field, or a username field of
    // a login in two steps): checked after loads and now and then (forms appear later).
    property bool pageHasLogin: false
    function checkLogin() {
        if (!current || !loginSheet.available) {
            pageHasLogin = false
            return
        }
        const view = current
        view.runJavaScript(
            "!!document.querySelector('input[type=password], input[autocomplete~=username], input[autocomplete~=one-time-code]')",
            (r) => { if (view === win.current) win.pageHasLogin = !!r })
    }
    Timer { interval: 2500; repeat: true; running: win.active && !!win.current; onTriggered: win.checkLogin() }
    onCurrentChanged: checkLogin()
    readonly property string fillScript: "(function(u, p, code) {
        const set = (el, v) => {
            Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, v);
            el.dispatchEvent(new Event('input', {bubbles: true}));
            el.dispatchEvent(new Event('change', {bubbles: true}));
        };
        const shown = e => e.offsetParent !== null && !e.disabled && !e.readOnly;
        const inputs = [...document.querySelectorAll('input')].filter(shown);
        if (code !== null) {
            const f = inputs.find(e => /one-time-code/.test(e.autocomplete) || /otp|code|token|2fa|totp|mfa/i.test(e.name + e.id)
                                       || (e.inputMode === 'numeric' && e.type !== 'password'));
            if (f) set(f, code);
            return !!f;
        }
        const pw = inputs.find(e => e.type === 'password');
        const texts = inputs.filter(e => /^(text|email|tel)$/.test(e.type || 'text'));
        const user = pw ? texts.filter(e => e.compareDocumentPosition(pw) & Node.DOCUMENT_POSITION_FOLLOWING).pop()
                        : texts.find(e => /user|mail|login|account/i.test(e.name + e.id + e.autocomplete)) || texts[0];
        if (user && u) set(user, u);
        if (pw && p) set(pw, p);
        return !!(user || pw);
    })"
    function fillLogin(username, password) {
        if (current)
            current.runJavaScript(fillScript + "(" + JSON.stringify(username) + "," + JSON.stringify(password) + ", null)")
    }
    function fillCode(code) {
        if (current)
            current.runJavaScript(fillScript + "(null, null, " + JSON.stringify(code) + ")")
    }

    // for the agent (org.mobarchy.Browser)
    function publish() {
        BrowserBus.titles = tabs.map(t => t.title)
        BrowserBus.urls = tabs.map(t => t.url.toString())
        BrowserBus.current = index
    }
    Connections {
        target: BrowserBus
        function onOpenRequested(url) {
            win.newTab(url)
            win.raise()
            win.requestActivate()
        }
    }

    Component.onCompleted: {
        defaultAgent = profile.httpUserAgent
        applyAgent()
        const start = BrowserBus.startUrls()
        if (start.length)
            start.forEach(u => newTab(u))
        else
            newTab(homeUrl)
    }

    // --- the window --------------------------------------------------------------------
    Item {
        id: page
        anchors.fill: parent
    }

    BottomBar {
        id: bar
        anchors { left: parent.left; right: parent.right }
        y: win.barShown && !win.fullScreen ? parent.height - height : parent.height
        Behavior on y { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
        title: win.current ? win.current.title : ""
        host: win.current && win.current.url.toString().startsWith("http")
              ? new URL(win.current.url.toString()).hostname.replace(/^www\./, "") : ""
        secure: !win.current || !win.current.url.toString().startsWith("http:")
        progress: win.current && win.current.loading ? Math.max(0.05, win.current.loadProgress / 100) : 1
        canGoBack: win.current ? win.current.canGoBack : false
        tabCount: win.tabs.length
        login: win.pageHasLogin
        onShowLogins: loginSheet.open(bar.host)
        onBack: if (win.current) win.current.goBack()
        onForward: if (win.current) win.current.goForward()
        onEditAddress: address.open()
        onShowTabs: tabsView.visible = true
        onShowMenu: menu.open()
    }

    AddressSheet {
        id: address
        anchors.fill: parent
        current: win.current ? win.current.url.toString() : ""
        history: saved.visits
        onGo: (text) => { address.close(); win.load(text) }
        onCancel: address.close()
    }

    TabsView {
        id: tabsView
        anchors.fill: parent
        tabs: win.tabs
        current: win.index
        onPick: (i) => { win.index = i; win.barShown = true; tabsView.visible = false; win.publish() }
        onClose: (i) => win.closeTab(i)
        onNewTab: { tabsView.visible = false; win.newTab(""); address.open() }
        onDone: tabsView.visible = false
    }

    MenuSheet {
        id: menu
        anchors.fill: parent
        canGoForward: win.current ? win.current.canGoForward : false
        desktopSite: win.desktopSite
        textScale: win.current ? win.current.zoomFactor : 1
        onForward: if (win.current) win.current.goForward()
        onReload: if (win.current) win.current.reload()
        onNewTab: { win.newTab(""); address.open() }
        onCopyLink: {
            clip.text = win.current ? win.current.url.toString() : ""
            clip.selectAll(); clip.copy()
            toast.show(qsTr("Link copied"))
        }
        passwords: loginSheet.available
        onLogins: loginSheet.open(bar.host)
        onToggleDesktop: {
            win.desktopSite = !win.desktopSite
            win.applyAgent()
            if (win.current) win.current.reload()
        }
        onTextSmaller: if (win.current) win.current.zoomFactor = Math.max(0.5, win.current.zoomFactor - 0.1)
        onTextBigger: if (win.current) win.current.zoomFactor = Math.min(3, win.current.zoomFactor + 0.1)
    }
    TextEdit { id: clip; visible: false }

    LoginSheet {
        id: loginSheet
        anchors.fill: parent
        onFill: (username, password) => win.fillLogin(username, password)
        onFillCode: (code) => win.fillCode(code)
    }

    // A site asks for the camera, the microphone, the location, notifications, ...
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

    // A short note over the bar.
    Rectangle {
        id: toast
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: bar.height + 16 }
        width: toastText.implicitWidth + 32; height: 40; radius: 20
        color: Theme.panel
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        function show(text) { toastText.text = text; opacity = 1; toastTimer.restart() }
        Text { id: toastText; anchors.centerIn: parent; color: Theme.fg; font { family: Theme.font; pixelSize: 14 } }
        Timer { id: toastTimer; interval: 2200; onTriggered: toast.opacity = 0 }
    }
}
