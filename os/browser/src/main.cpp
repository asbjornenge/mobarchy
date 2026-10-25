// mobarchy-browser: Mobarchy's browser (ADR 0023). QtWebEngine with a phone's chrome; one
// process: a second start hands its URLs to the first over D-Bus.
//
// mobarchy-browser --app=URL --app-id=ID: a web app on the home screen (mobarchy-webapp add),
// a process and window of its own (Wayland app ID = ID, which its desktop entry names as
// StartupWMClass), with its own profile and no browser chrome; a second start of the same
// app shows it.
#include "browserbus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char *argv[])
{
    // The page's viewport as a phone's (meta viewport, device-width), and touch as touch.
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    // (BackForwardCache: a page gone back to is the one left, kept: where it was scrolled,
    // nothing loaded again; WebRtcPipeWireCamera: the phone's cameras are PipeWire's (through
    // the camera portal), not V4L2 devices)
    flags += " --enable-viewport --touch-events=enabled"
             " --enable-features=OverlayScrollbar,BackForwardCache,WebRtcPipeWireCamera";
    // Widevine (DRM: Spotify, Netflix), where widevine-installer put the arm64 CDM
    const char *widevine = "/var/lib/widevine/WidevineCdm/_platform_specific/linux_arm64/libwidevinecdm.so";
    if (QFile::exists(QString::fromLatin1(widevine)))
        flags += QByteArray(" --widevine-path=") + widevine;
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.trimmed());
    if (!qEnvironmentVariableIsSet("QML_NO_TOUCH_COMPRESSION"))
        qputenv("QML_NO_TOUCH_COMPRESSION", "1");

    // (before the application object: the app ID is the window's)
    QString appUrl, appId;
    for (int i = 1; i < argc; i++) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QLatin1String("--app=")))
            appUrl = arg.mid(6);
        else if (arg.startsWith(QLatin1String("--app-id=")))
            appId = arg.mid(9);
    }
    const bool webApp = !appUrl.isEmpty() && !appId.isEmpty();

    QtWebEngineQuick::initialize();
    QGuiApplication::setDesktopFileName(webApp ? appId : QStringLiteral("org.mobarchy.Browser"));
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mobarchy-browser"));
    app.setOrganizationName(QStringLiteral("mobarchy"));

    QStringList urls;
    if (webApp)
        urls << appUrl;
    else
        for (const QString &arg : app.arguments().mid(1))
            if (!arg.startsWith(QLatin1Char('-')))
                urls << QUrl::fromUserInput(arg, QDir::currentPath()).toString();

    // A web app's bus name: org.mobarchy.WebApp.<its ID, as a D-Bus name element>
    QString service = QString::fromLatin1(BrowserBus::Service);
    if (webApp) {
        QString element = appId;
        element.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_]")), QStringLiteral("_"));
        service = QStringLiteral("org.mobarchy.WebApp._") + element;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    BrowserBus *browser = BrowserBus::instance();
    if (!bus.registerService(service)) {
        if (webApp) {   // already open: show it
            bus.call(QDBusMessage::createMethodCall(service, QString::fromLatin1(BrowserBus::Path),
                     QString::fromLatin1(BrowserBus::Service), QStringLiteral("Show")));
            return 0;
        }
        // already running: the URLs go there (it shows itself), or it just shows itself
        auto call = [&](const QString &method) {
            return QDBusMessage::createMethodCall(QString::fromLatin1(BrowserBus::Service),
                QString::fromLatin1(BrowserBus::Path), QString::fromLatin1(BrowserBus::Service), method);
        };
        for (const QString &url : urls)
            bus.call(call(QStringLiteral("Open")) << url);
        if (urls.isEmpty())
            bus.call(call(QStringLiteral("Show")));
        return 0;
    }
    bus.registerObject(QString::fromLatin1(BrowserBus::Path), browser,
                       QDBusConnection::ExportScriptableSlots);
    browser->setStartUrls(urls);

    // QML's BrowserBus: the one on D-Bus (a context property: registering it into the
    // module at run time would hide the module's own types)
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("BrowserBus"), browser);
    engine.rootContext()->setContextProperty(QStringLiteral("appId"), appId);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Mobarchy.Browser", webApp ? "WebApp" : "Main");
    return app.exec();
}
