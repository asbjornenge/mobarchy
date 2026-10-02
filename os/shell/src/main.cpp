// mobarchy-shell: status bar, bottom edge with swipe-up app drawer, and the agent home
// screen. See ADR 0009.
#include "iconprovider.h"

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

    // The app_id of the shell's own windows: the window list leaves them out.
    QGuiApplication::setDesktopFileName(QStringLiteral("org.mobarchy.Shell"));
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("mobarchy-shell"));
    // Layer surfaces are closed by the compositor when their output goes away (a fold);
    // the shell puts them back, so a moment without windows is not the end.
    app.setQuitOnLastWindowClosed(false);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("icon"), new IconProvider(qEnvironmentVariable(
        "MOBARCHY_ICON_THEME", QStringLiteral("Adwaita"))));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Mobarchy.Shell", "Main");
    return app.exec();
}
