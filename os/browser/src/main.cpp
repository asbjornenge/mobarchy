// mobarchy-browser: Mobarchy's browser (ADR 0023). QtWebEngine with a phone's chrome; one
// process: a second start hands its URLs to the first over D-Bus.
#include "browserbus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

int main(int argc, char *argv[])
{
    // The page's viewport as a phone's (meta viewport, device-width), and touch as touch.
    QByteArray flags = qgetenv("QTWEBENGINE_CHROMIUM_FLAGS");
    flags += " --enable-viewport --touch-events=enabled --enable-features=OverlayScrollbar";
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", flags.trimmed());
    if (!qEnvironmentVariableIsSet("QML_NO_TOUCH_COMPRESSION"))
        qputenv("QML_NO_TOUCH_COMPRESSION", "1");

    QtWebEngineQuick::initialize();
    QGuiApplication::setDesktopFileName(QStringLiteral("org.mobarchy.Browser"));
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mobarchy-browser"));
    app.setOrganizationName(QStringLiteral("mobarchy"));

    QStringList urls;
    for (const QString &arg : app.arguments().mid(1))
        if (!arg.startsWith(QLatin1Char('-')))
            urls << QUrl::fromUserInput(arg, QDir::currentPath()).toString();

    QDBusConnection bus = QDBusConnection::sessionBus();
    BrowserBus *browser = BrowserBus::instance();
    if (!bus.registerService(QString::fromLatin1(BrowserBus::Service))) {
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
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Mobarchy.Browser", "Main");
    return app.exec();
}
