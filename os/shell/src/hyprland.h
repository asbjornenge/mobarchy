// Hyprland's own IPC, where the Wayland protocols don't reach (laptop mode's fullscreen): its
// events (.socket2.sock: "fullscreen>>1") and its commands (.socket.sock: "dispatch ...",
// "j/activewindow"), in $XDG_RUNTIME_DIR/hypr/$HYPRLAND_INSTANCE_SIGNATURE.
#pragma once

#include <QLocalSocket>
#include <QObject>
#include <QTimer>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

class Hyprland : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit Hyprland(QObject *parent = nullptr);

    // A command ("fullscreenstate 1 2"), to the window in front; Hyprland's answer ("ok")
    Q_INVOKABLE QString dispatch(const QString &args);
    // A query, as JSON ("activewindow", "clients"); null if Hyprland can't be asked
    Q_INVOKABLE QVariant query(const QString &what);

signals:
    // a window went fullscreen, or came out of it (on the workspace in view)
    void fullscreenEvent();

private:
    QString socketPath(const QString &name) const;
    QByteArray request(const QByteArray &command);
    void listen();
    QLocalSocket m_events;
    QByteArray m_pending;
    QTimer m_retry;
};
