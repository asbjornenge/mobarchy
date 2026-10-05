// The lock screen's PIN (ADR 0013). The PIN is the owner's account password: it is checked
// with PAM (/etc/pam.d/mobarchy-lock, which also unlocks the keyring with it) and set through
// AccountsService. The default keyring is re-encrypted with the PIN when it changes.
// Wrong PINs cost time: after a few, each attempt waits longer (kept across restarts).
#pragma once

#include <QDateTime>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <functional>

class PinLock : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasPin READ hasPin NOTIFY hasPinChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Seconds until the next attempt is allowed (0: now).
    Q_PROPERTY(int wait READ wait NOTIFY waitChanged)
    // The PIN has been given since the phone started (the keyring is open): until then, only
    // the PIN unlocks (not a finger), as on Android.
    Q_PROPERTY(bool givenSinceBoot READ givenSinceBoot NOTIFY givenSinceBootChanged)

public:
    explicit PinLock(QObject *parent = nullptr);

    bool hasPin() const { return m_hasPin; }
    bool busy() const { return m_busy; }
    int wait() const;
    bool givenSinceBoot() const { return m_givenSinceBoot; }

    Q_INVOKABLE void verify(const QString &pin);
    // Set or change the PIN (oldPin: the current one, empty if there is none).
    Q_INVOKABLE void change(const QString &oldPin, const QString &newPin);
    Q_INVOKABLE void remove(const QString &oldPin);
    Q_INVOKABLE void refresh();
    // The shell says when the lock screen is up. With a PIN, that is published for
    // mobarchy-mcp (the agent may not use the screen of a locked phone): the file
    // $XDG_RUNTIME_DIR/mobarchy/locked exists.
    Q_INVOKABLE void setLocked(bool locked);

    static constexpr int MinLength = 4;

signals:
    void hasPinChanged();
    void busyChanged();
    void waitChanged();
    void givenSinceBootChanged();
    void verified(bool ok);
    void changed(bool ok, const QString &error);

private:
    // PAM runs in a thread (it may take a moment); done() gets the result on this thread.
    void check(const QString &pin, std::function<void(bool)> done);
    void counted(bool ok);
    void setBusy(bool busy);
    bool setPassword(const QString &newPin, QString *error);
    bool rekey(const QString &oldPin, const QString &newPin, QString *error);
    void loadState();
    void setGiven();
    static QString runtimeDir();
    void saveState();

    QString m_userPath;  // AccountsService's object for the owner
    bool m_hasPin = false;
    bool m_busy = false;
    bool m_givenSinceBoot = false;
    int m_failures = 0;
    QDateTime m_waitUntil;
    QTimer m_tick;
};
