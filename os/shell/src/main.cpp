// mobarchy-shell: status bar, bottom edge with swipe-up app drawer, and the agent home
// screen. See ADR 0009.
//   mobarchy-shell --store    the app store (ADR 0012), an app of its own
//   mobarchy-shell --camera   the camera (ADR 0018), an app of its own
//   mobarchy-shell --themes   the themes (ADR 0025), an app of its own
//   mobarchy-shell --clock    alarms and timers (ADR 0028), an app of its own
//   mobarchy-shell --photos [path]   the photos in ~/Pictures, an app of its own
#include "iconprovider.h"
#include "windowcapture.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    // Deliver touch moves as they come instead of batching them to the next frame. The
    // batching schedules a frame on the window that has the touch (the bottom edge) for
    // every move; with the drawer animating in its own window at the same time, the two
    // windows' frames alternate and both drop to half the refresh rate.
    if (!qEnvironmentVariableIsSet("QML_NO_TOUCH_COMPRESSION"))
        qputenv("QML_NO_TOUCH_COMPRESSION", "1");

    // The apps in the shell's binary: --store, --camera. The app_id of the shell's own
    // windows: the window list leaves them out. The apps are apps like any other.
    struct App { const char *flag, *id, *name, *qml; };
    static const App apps[] = {
        {"--store", "org.mobarchy.Store", "mobarchy-store", "Store"},
        {"--camera", "org.mobarchy.Camera", "mobarchy-camera", "CameraApp"},
        {"--themes", "org.mobarchy.Themes", "mobarchy-themes", "ThemesApp"},
        {"--clock", "org.mobarchy.Clock", "mobarchy-clock", "ClockApp"},
        {"--photos", "org.mobarchy.Photos", "mobarchy-photos", "PhotosApp"},
        {"--phone", "org.mobarchy.Phone", "mobarchy-phone", "PhoneApp"},
        {"--messages", "org.mobarchy.Messages", "mobarchy-messages", "MessagesApp"},
        {"--accounts", "org.mobarchy.Accounts", "mobarchy-accounts-app", "AccountsApp"},
        {"--calendar", "org.mobarchy.Calendar", "mobarchy-calendar-app", "CalendarApp"},
    };
    const App *own = nullptr;
    for (const App &a : apps)
        if (argc > 1 && qstrcmp(argv[1], a.flag) == 0)
            own = &a;
    QGuiApplication::setDesktopFileName(QString::fromLatin1(own ? own->id : "org.mobarchy.Shell"));
    QGuiApplication app(argc, argv);
    app.setApplicationName(QString::fromLatin1(own ? own->name : "mobarchy-shell"));
    // Layer surfaces are closed by the compositor when their output goes away (a fold);
    // the shell puts them back, so a moment without windows is not the end.
    app.setQuitOnLastWindowClosed(own != nullptr);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("windows"), new WindowImages);
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider(qEnvironmentVariable(
        "MOBARCHY_ICON_THEME", QStringLiteral("Adwaita"))));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Mobarchy.Shell", own ? own->qml : "Main");
    return app.exec();
}
