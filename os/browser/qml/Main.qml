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
    title: current && current.title ? current.title : qsTr("Bifrost")

    property var tabs: []
    property int index: 0
    readonly property var current: tabs.length > index ? tabs[index] : null

    // Back in front after a while behind another app: the page shown again. QtWebEngine
    // only counts the view hidden, not the window under another, and on the phone's GPU a
    // page left behind for a while came back black until touched; hidden and shown again,
    // Chromium draws it anew (as a tab switched back to).
    property bool reshowing: false
    property real inactiveSince: 0
    onActiveChanged: {
        if (!active) {
            inactiveSince = Date.now()
        } else if (inactiveSince > 0 && Date.now() - inactiveSince > 2000) {
            reshowing = true
            Qt.callLater(() => win.reshowing = false)
        }
    }
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
    // The open tabs, for the next start: their URLs and which was in front.
    Settings {
        id: session
        category: "session"
        property var urls: []
        property int current: 0
    }
    function saveSession() {
        session.urls = tabs.map(t => t.url.toString()).filter(u => u.startsWith("http"))
        session.current = Math.min(index, Math.max(0, session.urls.length - 1))
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
            // The bar floats over the page: a page that changed size with the bar would be
            // laid out again on every swipe that shows or hides it, and jump.
            anchors.fill: parent
            visible: win.current === view && !win.reshowing
            profile: profile
            settings.scrollAnimatorEnabled: true
            settings.fullScreenSupportEnabled: true
            settings.touchIconsEnabled: true
            property real lastY: 0
            // a picture of the page as it was last shown (the tabs' list shows it)
            property var shot: null
            // Back (or forward) to where the page was scrolled: where each page was left is
            // kept, and gone back to, it is scrolled there again, again and again for a few
            // seconds while a page that fills in as it goes (a list of results) grows, until
            // the finger touches it. (Chromium's own keeps it only for pages it may cache.)
            property var scrolledTo: ({})
            property real seenY: 0
            property string shownUrl: ""
            property real restoreY: -1
            onUrlChanged: {
                if (shownUrl !== "" && shownUrl !== url.toString())
                    scrolledTo[shownUrl] = seenY
                shownUrl = url.toString()
                if (restoreY > 0)
                    restoreSoon.restart()
            }
            onNavigationRequested: (request) => {
                const y = scrolledTo[request.url.toString()]
                restoreY = request.navigationType === WebEngineNavigationRequest.BackForwardNavigation && y > 0 ? y : -1
            }
            function restoreScroll() {
                if (restoreY <= 0)
                    return
                const y = Math.round(restoreY)
                restoreY = -1
                runJavaScript("(function(y) { var n = 0, stop = false;"
                    + " addEventListener('touchstart', function() { stop = true }, {once: true, capture: true});"
                    + " (function go() { if (stop) return; window.scrollTo(0, y);"
                    + "   if (Math.abs(window.scrollY - y) > 4 && n++ < 30) setTimeout(go, 200); })(); })(" + y + ")")
            }
            Timer { id: restoreSoon; interval: 150; onTriggered: view.restoreScroll() }
            onScrollPositionChanged: {
                seenY = scrollPosition.y
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
                    view.restoreScroll()
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

    // The tabs' list, from the page in front: a picture of it first (its card shows it, and
    // it shrinks into the card)
    function showTabs() {
        const tab = current
        if (!tab || !tab.visible || tab.width <= 0) {
            tabsView.open(false)
            return
        }
        const asked = tab.grabToImage((result) => {
            tab.shot = result
            if (!tabsView.visible)
                tabsView.open(true)
        }, Qt.size(tab.width / 2, tab.height / 2))
        // (no picture, or none in time: the list without it)
        if (asked)
            showTabsAnyway.restart()
        else
            tabsView.open(false)
    }
    Timer { id: showTabsAnyway; interval: 400; onTriggered: if (!tabsView.visible) tabsView.open(false) }

    // for the agent (org.mobarchy.Browser)
    function publish() {
        saveSession()
        BrowserBus.titles = tabs.map(t => t.title)
        BrowserBus.urls = tabs.map(t => t.url.toString())
        BrowserBus.current = index
    }
    Connections {
        target: BrowserBus
        function onWebAppAdded(ok, message) {
            toast.show(ok ? qsTr("On the home screen: %1").arg(message) : qsTr("Could not add it: %1").arg(message))
        }
        function onOpenRequested(url) {
            win.newTab(url)
            win.raise()
            win.requestActivate()
        }
        function onShowRequested() {
            win.raise()
            win.requestActivate()
        }
        function onCloseRequested(i) { win.closeTab(i) }
    }

    Component.onCompleted: {
        defaultAgent = profile.httpUserAgent
        applyAgent()
        // last time's tabs, then what this start was given
        const restored = session.urls.slice()
        const front = session.current
        restored.forEach(u => newTab(u))
        const start = BrowserBus.startUrls()
        start.forEach(u => newTab(u))
        if (!tabs.length)
            newTab(homeUrl)
        else if (!start.length)
            index = Math.min(front, tabs.length - 1)
    }

    // --- the window --------------------------------------------------------------------
    Item {
        id: page
        // (moved down by a pull to refresh)
        width: parent.width; height: parent.height
    }
    // pull to refresh, the page at its top
    PullRefresh {
        view: win.current
        page: page
    }
    // back and forward: a swipe in from the left edge, and from the right
    EdgeSwipe {
        enabled_: !!win.current && win.current.canGoBack
        onGo: win.current.goBack()
    }
    EdgeSwipe {
        fromRight: true
        enabled_: !!win.current && win.current.canGoForward
        onGo: win.current.goForward()
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
        onShowTabs: win.showTabs()
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
        onAddToHome: {
            if (!win.current || !win.current.url.toString().startsWith("http"))
                return
            toast.show(qsTr("Adding to the home screen …"))
            BrowserBus.addWebApp(win.current.url.toString(), "")
        }
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
    PermissionSheet {
        id: permissionSheet
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
