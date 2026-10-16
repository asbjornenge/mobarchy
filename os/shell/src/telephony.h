// Telephony through ofono on the system bus (org.ofono): the first modem (/ril_0), its SIM
// (presence, PIN), network registration (operator, signal), calls and SMS (ADR 0031). ofono
// drives the modem through the Android container's rild (ADR 0014); the shell is a client,
// the same D-Bus-client shape as Bluetooth (BlueZ).
//
// Incoming SMS are kept by the shell in ~/.local/share/mobarchy/messages.json (threads by
// number), so the agent can read and write them too (the alarms' pattern, ADR 0028).
#pragma once

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>


class Telephony : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // ofono runs and there is a modem.
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // the modem's radio is on (set once the SIM is unlocked)
    Q_PROPERTY(bool online READ online NOTIFY changed)
    Q_PROPERTY(bool simPresent READ simPresent NOTIFY changed)
    // "none" (unlocked or no lock), "pin", "puk", "pin2", "puk2", ... : what the SIM wants
    Q_PROPERTY(QString pinRequired READ pinRequired NOTIFY changed)
    // how many tries are left for pinRequired before it locks to the next level (-1: unknown)
    Q_PROPERTY(int pinRetries READ pinRetries NOTIFY changed)
    Q_PROPERTY(QString operatorName READ operatorName NOTIFY changed)
    // "unregistered", "registered", "searching", "denied", "unknown", "roaming"
    Q_PROPERTY(QString registration READ registration NOTIFY changed)
    Q_PROPERTY(int signalStrength READ signalStrength NOTIFY changed)  // 0..100
    // the calls now: [{path, state, number, name, incoming, active}], newest last
    Q_PROPERTY(QVariantList calls READ calls NOTIFY callsChanged)

public:
    explicit Telephony(QObject *parent = nullptr);

    bool available() const { return !m_modem.isEmpty(); }
    bool online() const { return m_online; }
    bool simPresent() const { return m_simPresent; }
    QString pinRequired() const { return m_pinRequired; }
    int pinRetries() const { return m_pinRetries; }
    QString operatorName() const { return m_operator; }
    QString registration() const { return m_registration; }
    int signalStrength() const { return m_signal; }
    QVariantList calls() const { return m_calls; }

    // unlock the SIM: type is pinRequired ("pin"/"puk"/...); for a PUK, newPin is set too
    Q_INVOKABLE void enterPin(const QString &pin);
    Q_INVOKABLE void resetPin(const QString &puk, const QString &newPin);
    // turn the radio on/off (flight mode leaves the SIM but drops the radio)
    Q_INVOKABLE void setOnline(bool on);

    Q_INVOKABLE void dial(const QString &number);
    Q_INVOKABLE void answer(const QString &path);
    Q_INVOKABLE void hangup(const QString &path);
    Q_INVOKABLE void hangupAll();
    Q_INVOKABLE void sendTones(const QString &tones);  // DTMF while in a call

    Q_INVOKABLE void sendMessage(const QString &to, const QString &text);
    // the SMS threads: { number: [{text, incoming, time}] } (from messages.json, ADR 0031)
    Q_INVOKABLE QVariantMap threads() const;

signals:
    void changed();
    void callsChanged();
    void messagesChanged();
    // a call is ringing (the Phone app / lock screen shows it, rings; the alarm's pattern)
    void incomingCall(const QString &path, const QString &number);
    void messageReceived(const QString &from, const QString &text);
    void failed(const QString &what, const QString &reason);

private slots:
    void modemAdded(const QDBusObjectPath &path, const QVariantMap &properties);
    void modemRemoved(const QDBusObjectPath &path);
    void modemPropertyChanged(const QString &name, const QDBusVariant &value);
    void simPropertyChanged(const QString &name, const QDBusVariant &value);
    void networkPropertyChanged(const QString &name, const QDBusVariant &value);
    void callAdded(const QDBusObjectPath &path, const QVariantMap &properties);
    void callRemoved(const QDBusObjectPath &path);
    void callPropertyChanged(const QString &name, const QDBusVariant &value, const QDBusMessage &message);
    void incomingMessage(const QString &text, const QVariantMap &info);
    void serviceUp();
    void serviceDown();

private:
    void findModems();
    void useModem(const QString &path, const QVariantMap &properties);
    void dropModem();
    void watchInterfaces(const QStringList &interfaces);
    void refreshSim();
    void refreshNetwork();
    void refreshCalls();
    void rebuildCalls();
    void storeMessage(const QString &number, const QString &text, bool incoming);

    QString m_modem;                 // the active modem's object path ("/ril_0")
    QStringList m_interfaces;        // the modem's ofono interfaces now
    bool m_online = false;
    bool m_simPresent = false;
    QString m_pinRequired = QStringLiteral("none");
    int m_pinRetries = -1;
    QString m_operator;
    QString m_registration = QStringLiteral("unregistered");
    int m_signal = 0;
    struct Call {
        QString state, number, name;
    };
    QHash<QString, Call> m_callMap;  // path -> call
    QVariantList m_calls;
    QSet<QString> m_ringing;         // paths we've already announced as incoming
};
