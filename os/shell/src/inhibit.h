// Apps that keep the screen on (a playing video): the org.freedesktop.ScreenSaver
// Inhibit/UnInhibit service on the session bus, which Firefox, Chromium, mpv and others call.
// An app's inhibitions end when it says so or when it leaves the bus (quits, crashes).
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class QDBusServiceWatcher;

class Inhibitors : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // Some app wants the screen to stay on.
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool registered READ registered CONSTANT)

public:
    explicit Inhibitors(QObject *parent = nullptr);
    bool active() const { return !m_inhibitions.isEmpty(); }
    bool registered() const { return m_registered; }

    // Called by the D-Bus adaptor; sender is the caller's unique bus name.
    uint inhibit(const QString &app, const QString &reason, const QString &sender);
    void uninhibit(uint cookie, const QString &sender);

signals:
    void activeChanged();

private:
    struct Inhibition {
        QString app, reason, sender;
    };
    void senderGone(const QString &sender);

    QHash<uint, Inhibition> m_inhibitions;
    QDBusServiceWatcher *m_watcher;
    uint m_nextCookie = 1;
    bool m_registered = false;
};
