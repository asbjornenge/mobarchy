// The session's polkit authentication agent (ADR 0024): when something asks polkit for an
// action the owner must authorize (pkexec, run0, 1Password's system unlock, systemd's
// admin actions), the shell asks: a finger on the sensor or the PIN.
//
// PAM (/etc/pam.d/polkit-1) runs pam_fprintd first, which waits for a finger; a PIN typed
// meanwhile ends that wait (org.mobarchy.Fingerprint.SkipVerify), and the PIN answers the
// password prompt that follows. On a locked phone a request waits for the unlock.
#pragma once

#include <QObject>
#include <QPointer>
#include <QtQml/qqmlregistration.h>

namespace PolkitQt1::Agent {
class AsyncResult;
class Session;
}
class AgentListener;

class PolkitAgent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // A request is waiting for the owner (the sheet is up when the phone isn't locked).
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // polkit's message ("Authentication is needed to run `/usr/bin/pacman -S ...' as the
    // super user").
    Q_PROPERTY(QString message READ message NOTIFY changed)
    // pam_fprintd is waiting for a finger.
    Q_PROPERTY(bool fingerWanted READ fingerWanted NOTIFY changed)
    // A PIN is being checked.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What went wrong last (a wrong PIN, a finger that didn't match).
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The shell says so while the phone is locked: the request waits.
    Q_PROPERTY(bool held READ held WRITE setHeld NOTIFY changed)
    Q_PROPERTY(bool registered READ registered CONSTANT)

public:
    explicit PolkitAgent(QObject *parent = nullptr);
    ~PolkitAgent() override;

    bool active() const { return m_result != nullptr; }
    QString message() const { return m_message; }
    bool fingerWanted() const { return m_fingerWanted; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    bool held() const { return m_held; }
    void setHeld(bool held);
    bool registered() const { return m_registered; }

    Q_INVOKABLE void enterPin(const QString &pin);
    Q_INVOKABLE void cancel();

    // From the listener.
    void begin(const QString &actionId, const QString &message, const QString &identity,
               const QString &cookie, PolkitQt1::Agent::AsyncResult *result);
    void cancelled();

signals:
    void changed();
    void authorized();
    void failed();      // a wrong PIN (the pad shakes)

private:
    void start(const QString &pin = {});
    void stopSession();
    void finish();
    void sessionCompleted(bool gained);
    void respond();

    AgentListener *m_listener;
    bool m_registered = false;
    PolkitQt1::Agent::AsyncResult *m_result = nullptr;
    QPointer<PolkitQt1::Agent::Session> m_session;
    QString m_identity, m_cookie, m_message, m_error;
    QString m_pin;              // typed, for the password prompt
    bool m_passwordWanted = false;
    bool m_responded = false;   // this conversation got a PIN
    bool m_fingerWanted = false;
    bool m_busy = false;
    bool m_held = false;
    bool m_cancelling = false;  // the owner, or polkit, gave up
};
