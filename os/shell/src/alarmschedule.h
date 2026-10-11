// When alarms and timers ring (ADR 0028), from alarms.json as mobarchy-mcp's alarms.py
// writes it: times of day in local time, ISO weekdays (Monday 1; none: once), epoch seconds.
// No GUI: the tests link it alone.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace alarmschedule {

struct Due {
    QString kind;   // "alarm" or "timer"
    QString id;
    QString label;
    QDateTime at;   // when it was to ring
    bool once = false;  // an alarm without days (it turns itself off when stopped)
};

// An alarm's ring times around now: the last one at or before it, the next one after it
// (a snooze counts, when it is the nearer one). None when it is off.
std::optional<QDateTime> lastRing(const QJsonObject &alarm, const QDateTime &now);
std::optional<QDateTime> nextRing(const QJsonObject &alarm, const QDateTime &now);

// What should be ringing now: rings that came at most `grace` seconds ago (the shell may
// have been down at the time) and haven't rung yet (last_rung), nor came before the alarm
// was made or changed (created).
QList<Due> due(const QJsonObject &file, const QDateTime &now, int grace = 600);

// The soonest ring after now, of any alarm or timer; invalid if there is none.
QDateTime nextEvent(const QJsonObject &file, const QDateTime &now);

// The soonest alarm (not timer) to ring after now; invalid if there is none.
QDateTime nextAlarm(const QJsonObject &file, const QDateTime &now);

}  // namespace alarmschedule
