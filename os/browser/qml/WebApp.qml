pragma ComponentBehavior: Bound

import QtCore
import QtQuick
import QtQuick.Window
import QtWebEngine
import Mobarchy.Browser

// A web app on the home screen (mobarchy-browser --app=URL --app-id=ID): the site alone, as
// a phone app, without the browser's bar; its own profile (its logins are its own); the
// phone's user agent, so sites send their mobile versions. Pages it opens in new windows
// (sign-ins, pop-ups) come over it with a close button; a swipe in from the left goes back.
Window {
    id: win
    width: 450; height: 900
    visible: true
    color: Theme.bg
    title: view.title || appId

    function mobileAgent(ua) {
        return ua.replace(/\([^)]*Linux[^)]*\)/, "(Linux; Android 15; Mobarchy)")
                 .replace(/ QtWebEngine\/[\d.]+/, "")
                 .replace(/ Safari\//, " Mobile Safari/")
    }

    WebEngineProfile {
        id: profile
        storageName: "app-" + appId
        offTheRecord: false
        persistentCookiesPolicy: WebEngineProfile.AllowPersistentCookies
        httpCacheType: WebEngineProfile.DiskHttpCache
        onDownloadRequested: (download) => {
            download.downloadDirectory = StandardPaths.writableLocation(StandardPaths.DownloadLocation)
                .toString().replace("file://", "")
            download.accept()
        }
        Component.onCompleted: httpUserAgent = win.mobileAgent(httpUserAgent)
    }

    WebEngineView {
        id: view
        anchors.fill: parent
        profile: profile
        url: BrowserBus.startUrls()[0]
        settings.scrollAnimatorEnabled: true
        settings.fullScreenSupportEnabled: true
        settings.touchIconsEnabled: true
        onNewWindowRequested: (request) => popup.open(request)
        onFullScreenRequested: (request) => {
            request.accept()
            win.visibility = request.toggleOn ? Window.FullScreen : Window.AutomaticVisibility
        }
        // (notifications and the clipboard, as an installed app has them; the camera, the
        // microphone and the location not yet: the browser asks for those)
        onPermissionRequested: (permission) => {
            if (permission.permissionType === WebEnginePermission.PermissionType.Notifications
                    || permission.permissionType === WebEnginePermission.PermissionType.ClipboardReadWrite)
                permission.grant()
            else
                permission.deny()
        }
    }

    // A window the page opened (a sign-in with Google, a pop-up): over the app, until it
    // closes itself or is closed.
    Item {
        id: popup
        anchors.fill: parent
        visible: false
        function open(request) {
            visible = true
            request.openIn(popupView)
        }
        Rectangle { anchors.fill: parent; color: Theme.bg }
        WebEngineView {
            id: popupView
            anchors { fill: parent; topMargin: 44 }
            profile: profile
            onWindowCloseRequested: popup.visible = false
            onNewWindowRequested: (request) => request.openIn(popupView)
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 44
            color: Theme.panel
            Text {
                anchors { left: parent.left; leftMargin: 16; right: closeButton.left; verticalCenter: parent.verticalCenter }
                text: popupView.title
                elide: Text.ElideRight
                color: Theme.fg
                font { family: Theme.font; pixelSize: 14 }
            }
            Text {
                id: closeButton
                anchors { right: parent.right; rightMargin: 16; verticalCenter: parent.verticalCenter }
                text: "󰅖"
                color: Theme.fg
                font { family: Theme.font; pixelSize: 20 }
                MouseArea {
                    anchors { fill: parent; margins: -12 }
                    onClicked: popup.visible = false
                }
            }
        }
    }

    // Back: a swipe in from the left edge
    Item {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 16
        DragHandler {
            id: edge
            target: null
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: {
                if (!active && centroid.position.x - centroid.pressPosition.x > 80) {
                    if (popup.visible)
                        popupView.canGoBack ? popupView.goBack() : (popup.visible = false)
                    else if (view.canGoBack)
                        view.goBack()
                }
            }
        }
    }

    Connections {
        target: BrowserBus
        function onShowRequested() {
            win.raise()
            win.requestActivate()
        }
    }
}
