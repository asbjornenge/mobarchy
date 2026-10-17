// The shell as GeoClue's agent (ADR 0033): GeoClue asks its agent whether an app may know
// where the phone is, and answers no one while there is none. Allowed: the shell itself and
// the agent layer (Frigg, which asks the owner its own way); apps the owner allowed in
// ~/.config/mobarchy/location.json ({"<desktop id>": true}). The others: no, for now (asking
// the owner, as for other permissions, comes with the first app that wants it).
#pragma once

#include <QDBusContext>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QtQml/qqmlregistration.h>

class LocationAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    QML_ELEMENT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.GeoClue2.Agent")
    Q_PROPERTY(uint MaxAccuracyLevel READ maxAccuracyLevel)

public:
    explicit LocationAgent(QObject *parent = nullptr);

    uint maxAccuracyLevel() const { return 8; }     // exact: the owner's own

public slots:
    // (D-Bus) may this app know where the phone is, and how exactly? Answers (authorized,
    // allowed accuracy level).
    void AuthorizeApp(const QString &desktopId, uint requestedAccuracyLevel);

private:
    void addAgent();
    QDBusServiceWatcher m_watcher;
};
