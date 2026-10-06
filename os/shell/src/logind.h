// Things the session may do through systemd-logind, without root: screen brightness
// (Session.SetBrightness) and restart / power off (Manager.Reboot / PowerOff).
#pragma once

#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

// The user's display session in logind: its ID and object path (empty: none). The shell runs
// as a user service, outside any session, so this is the session it serves.
struct DisplaySession {
    QString id, path;
};
DisplaySession displaySession();

class Brightness : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(qreal level READ level WRITE setLevel NOTIFY levelChanged)  // 0..1
    Q_PROPERTY(bool available READ available NOTIFY levelChanged)

public:
    explicit Brightness(QObject *parent = nullptr);

    qreal level() const { return m_level; }
    void setLevel(qreal level);
    bool available() const { return m_available; }

    // Re-read the level from the backlight that is on (after a fold, or another writer).
    Q_INVOKABLE void refresh();

signals:
    void levelChanged();

private:
    struct Backlight {
        QString name;
        int max = 0;
    };
    // The backlights of the panels that are on (bl_power == 0): with a fold, one of two.
    static QList<Backlight> activeBacklights();
    void apply();
    QString sessionPath();

    qreal m_level = 0;
    bool m_available = false;
    QString m_session;  // the user's display session, e.g. /org/freedesktop/login1/session/_31
    QTimer m_apply;     // a slider moves faster than it is worth writing the backlight
};

class Power : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    using QObject::QObject;

    Q_INVOKABLE void reboot();
    Q_INVOKABLE void powerOff();

private:
    void call(const QString &method);
};
