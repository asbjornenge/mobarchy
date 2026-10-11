// Alarms and timers (ADR 0028): the shell rings them. They are in
// ~/.local/share/mobarchy/alarms.json, which the agent (mobarchy-mcp's alarm tools) and the
// Clock app write; this follows the file, waits for the next ring, and when one is due shows
// it (AlarmScreen.qml) until it is snoozed or stopped. What ringing changes goes back into
// the file (last_rung, snoozed_until; a one-off alarm turns off, a timer goes), under the
// same lock the agent takes (flock on alarms.json.lock).
#pragma once

#include "alarmschedule.h"

#include <QDateTime>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QtQml/qqmlregistration.h>
#include <functional>

class Alarms : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool ringing READ ringing NOTIFY ringingChanged)
    Q_PROPERTY(QString kind READ kind NOTIFY ringingChanged)       // "alarm" or "timer"
    Q_PROPERTY(QString label READ label NOTIFY ringingChanged)
    Q_PROPERTY(QDateTime ringTime READ ringTime NOTIFY ringingChanged)
    // The next alarm to ring (not timers), for the lock screen and the status bar
    Q_PROPERTY(QDateTime nextAlarm READ nextAlarm NOTIFY changed)
    Q_PROPERTY(int snoozeMinutes READ snoozeMinutes CONSTANT)
    // The Clock app's: its own instance, which shows and changes them but leaves the
    // ringing to the shell's.
    Q_PROPERTY(bool ringer READ ringer WRITE setRinger NOTIFY ringerChanged)
    // [{id, time, days: [1..7], label, enabled, next}], by time of day
    Q_PROPERTY(QVariantList alarmList READ alarmList NOTIFY listChanged)
    // [{id, label, ends, duration}], the soonest first
    Q_PROPERTY(QVariantList timerList READ timerList NOTIFY listChanged)

public:
    explicit Alarms(QObject *parent = nullptr);

    bool ringing() const { return !m_queue.isEmpty(); }
    QString kind() const { return ringing() ? m_queue.first().kind : QString(); }
    QString label() const { return ringing() ? m_queue.first().label : QString(); }
    QDateTime ringTime() const { return ringing() ? m_queue.first().at : QDateTime(); }
    QDateTime nextAlarm() const { return m_nextAlarm; }

    int snoozeMinutes() const { return 9; }
    bool ringer() const { return m_ringer; }
    void setRinger(bool ringer);
    QVariantList alarmList() const;
    QVariantList timerList() const;

    // The Clock app's changes (as the agent's tools make them). time: "HH:mm"; days: ISO
    // weekdays, none for once.
    Q_INVOKABLE void addAlarm(const QString &time, const QString &label, const QVariantList &days);
    Q_INVOKABLE void updateAlarm(const QString &id, const QString &time, const QString &label,
                                 const QVariantList &days);
    Q_INVOKABLE void setEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void removeAlarm(const QString &id);
    Q_INVOKABLE void startTimer(int seconds, const QString &label);
    Q_INVOKABLE void cancelTimer(const QString &id);
    // The one ringing: again in a few minutes (an alarm), or no more.
    Q_INVOKABLE void snooze();
    Q_INVOKABLE void dismiss();

signals:
    void ringingChanged();
    void changed();
    void listChanged();
    void ringerChanged();
    // a ring has begun: wake the screen
    void started();

private:
    QString path() const;
    QJsonObject read() const;
    // Changes the file under the lock: read, change, write whole.
    void modify(const std::function<void(QJsonObject &)> &change);
    void reload();
    void check();
    void pop();
    // changes the alarm with this id (alarms.py's update: from now on, no snooze)
    void changeAlarm(const QString &id, const std::function<void(QJsonObject &)> &change);

    QFileSystemWatcher m_watcher;
    QTimer m_timer;
    QJsonObject m_file;
    QList<alarmschedule::Due> m_queue;
    QDateTime m_nextAlarm;
    bool m_ringer = true;
};
